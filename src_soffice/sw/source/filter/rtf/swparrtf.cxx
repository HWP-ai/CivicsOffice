/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <poolfmt.hxx>
#include <shellio.hxx>
#include <ndtxt.hxx>
#include <doc.hxx>
#include <docsh.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <swdll.hxx>
#include <swerror.h>

#include <unotextrange.hxx>

#include <unotools/streamwrap.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <com/sun/star/document/XFilter.hpp>
#include <com/sun/star/document/XImporter.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>

using namespace ::com::sun::star;

namespace
{
/// Glue class to call RtfImport as an internal filter, needed by copy&paste support.
class SwRTFReader : public Reader
{
    ErrCode Read(SwDoc& rDoc, const OUString& rBaseURL, SwPaM& rPam,
                 const OUString& rFileName) override;
};
}

ErrCode SwRTFReader::Read(SwDoc& rDoc, const OUString& /*rBaseURL*/, SwPaM& rPam,
                          const OUString& /*rFileName*/)
{
    if (!m_pStream)
        return ERR_SWG_READ_ERROR;

    // We want to work in an empty paragraph.
    // Step 1: XTextRange will be updated when content is inserted, so we know
    // the end position.
    const uno::Reference<text::XTextRange> xInsertPosition
        = SwXTextRange::CreateXTextRange(rDoc, *rPam.GetPoint(), nullptr);
    auto pSttNdIdx = std::make_shared<SwNodeIndex>(rDoc.GetNodes());
    const SwPosition* pPos = rPam.GetPoint();

    // Step 2: Split once and remember the node that has been split.
    rDoc.getIDocumentContentOperations().SplitNode(*pPos, false);
    *pSttNdIdx = pPos->GetNodeIndex() - 1;

    // Step 3: Split again.
    rDoc.getIDocumentContentOperations().SplitNode(*pPos, false);
    auto pSttNdIdx2 = std::make_shared<SwNodeIndex>(rDoc.GetNodes());
    *pSttNdIdx2 = pPos->GetNodeIndex();

    // Step 4: Insert all content into the new node
    rPam.Move(fnMoveBackward);
    rDoc.SetTextFormatColl(
        rPam, rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool(RES_POOLCOLL_STANDARD, false));

    SwDocShell* pDocShell(rDoc.GetDocShell());
    uno::Reference<lang::XMultiServiceFactory> xMultiServiceFactory(
        comphelper::getProcessServiceFactory());
    uno::Reference<uno::XInterface> xInterface(
        xMultiServiceFactory->createInstance("com.sun.star.comp.Writer.RtfFilter"),
        uno::UNO_SET_THROW);

    uno::Reference<document::XImporter> xImporter(xInterface, uno::UNO_QUERY_THROW);
    uno::Reference<lang::XComponent> xDstDoc(pDocShell->GetModel(), uno::UNO_QUERY_THROW);
    xImporter->setTargetDocument(xDstDoc);

    const uno::Reference<text::XTextRange> xInsertTextRange
        = SwXTextRange::CreateXTextRange(rDoc, *rPam.GetPoint(), nullptr);

    uno::Reference<document::XFilter> xFilter(xInterface, uno::UNO_QUERY_THROW);
    uno::Sequence<beans::PropertyValue> aDescriptor(comphelper::InitPropertySequence(
        { { "InputStream",
            uno::Any(uno::Reference<io::XStream>(new utl::OStreamWrapper(*m_pStream))) },
          { "InsertMode", uno::Any(true) },
          { "TextInsertModeRange", uno::Any(xInsertTextRange) } }));
    auto ret = ERRCODE_NONE;
    try
    {
        xFilter->filter(aDescriptor);
    }
    catch (uno::Exception const&)
    {
        TOOLS_WARN_EXCEPTION("sw.rtf", "SwRTFReader::Read()");
        ret = ERR_SWG_READ_ERROR;
    }

    // Clean up the fake paragraphs.
    SwUnoInternalPaM aPam(rDoc);
    ::sw::XTextRangeToSwPaM(aPam, xInsertPosition);
    if (pSttNdIdx->GetIndex())
    {
        // If we are in insert mode, join the split node that is in front
        // of the new content with the first new node. Or in other words:
        // Revert the first split node.
        SwTextNode* pTextNode = pSttNdIdx->GetNode().GetTextNode();
        SwNodeIndex aNxtIdx(*pSttNdIdx);
        if (pTextNode && pTextNode->CanJoinNext(&aNxtIdx)
            && pSttNdIdx->GetIndex() + 1 == aNxtIdx.GetIndex())
        {
            // If the PaM points to the first new node, move the PaM to the
            // end of the previous node.
            if (aPam.GetPoint()->GetNode() == aNxtIdx.GetNode())
            {
                aPam.GetPoint()->Assign(*pTextNode, pTextNode->GetText().getLength());
            }
            // If the first new node isn't empty, convert  the node's text
            // attributes into hints. Otherwise, set the new node's
            // paragraph style at the previous (empty) node.
            SwTextNode* pDelNd = aNxtIdx.GetNode().GetTextNode();
            if (pTextNode->GetText().getLength())
                pDelNd->FormatToTextAttr(pTextNode);
            else
            {
                pTextNode->ChgFormatColl(pDelNd->GetTextColl());
                if (!pDelNd->GetNoCondAttr(RES_PARATR_LIST_ID, /*bInParents=*/false))
                {
                    // Lists would need manual merging, but copy paragraph direct formatting
                    // otherwise.
                    pDelNd->CopyCollFormat(*pTextNode);
                }
            }
            pTextNode->JoinNext();
        }
    }

    if (pSttNdIdx2->GetIndex())
    {
        // If we are in insert mode, join the split node that is after
        // the new content with the last new node. Or in other words:
        // Revert the second split node.
        SwTextNode* pTextNode = pSttNdIdx2->GetNode().GetTextNode();
        SwNodeIndex aPrevIdx(*pSttNdIdx2);
        if (pTextNode && pTextNode->CanJoinPrev(&aPrevIdx)
            && pSttNdIdx2->GetIndex() - 1 == aPrevIdx.GetIndex())
        {
            // If the last new node isn't empty, convert  the node's text
            // attributes into hints. Otherwise, set the new node's
            // paragraph style at the next (empty) node.
            SwTextNode* pDelNd = aPrevIdx.GetNode().GetTextNode();
            if (pTextNode->GetText().getLength())
                pDelNd->FormatToTextAttr(pTextNode);
            else
                pTextNode->ChgFormatColl(pDelNd->GetTextColl());
            pTextNode->JoinPrev();
        }
    }

    return ret;
}

extern "C" SAL_DLLPUBLIC_EXPORT Reader* ImportRTF() { return new SwRTFReader; }

extern "C" SAL_DLLPUBLIC_EXPORT bool TestImportRTF(SvStream& rStream)
{
    SwGlobals::ensure();

    SfxObjectShellLock xDocSh(new SwDocShell(SfxObjectCreateMode::INTERNAL));
    xDocSh->DoInitNew();

    uno::Reference<lang::XMultiServiceFactory> xMultiServiceFactory(
        comphelper::getProcessServiceFactory());
    uno::Reference<uno::XInterface> xInterface(
        xMultiServiceFactory->createInstance("com.sun.star.comp.Writer.RtfFilter"),
        uno::UNO_SET_THROW);

    uno::Reference<document::XImporter> xImporter(xInterface, uno::UNO_QUERY_THROW);
    uno::Reference<lang::XComponent> xDstDoc(xDocSh->GetModel(), uno::UNO_QUERY_THROW);
    xImporter->setTargetDocument(xDstDoc);

    uno::Reference<document::XFilter> xFilter(xInterface, uno::UNO_QUERY_THROW);
    uno::Sequence<beans::PropertyValue> aDescriptor(comphelper::InitPropertySequence(
        { { "InputStream",
            uno::Any(uno::Reference<io::XStream>(new utl::OStreamWrapper(rStream))) } }));
    bool bRet = true;
    try
    {
        xFilter->filter(aDescriptor);
    }
    catch (...)
    {
        bRet = false;
    }
    return bRet;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

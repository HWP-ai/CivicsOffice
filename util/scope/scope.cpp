#include <map>
#include <string>
#include <vector>

class XMLNode {
};

class BaseBuildingSystemAgentEntry;
class AgentEntry;
class BaseBuildingSystemAgentComponent;
class AgentComponent;
class BaseBuildingSystemAgentEventContent;
class AgentEventContent;
class ScopeRuntime;
class ScopeLauncher;

class BaseBuildingSystemAgent {
};

class Agent: public BaseBuildingSystemAgent {    
    std::vector<AgentEntry> owningEntry;
    std::vector<AgentComponent> owningComponent;
    std::vector<AgentEventContent> owningEventContent;
};

class BaseBuildingSystemAgentEntry {
};

class AgentEntry: public BaseBuildingSystemAgentEntry {
public:
    union {
        int v;
        float f;
        std::string s;
    } val;
    int type;
    std::string Name;
};

class BaseBuildingSystemAgentComponent {
};

class AgentComponent: public BaseBuildingSystemAgentComponent {
public:
    AgentComponent(std::string name, Agent* owner){
    }
    void DoTask(std::string name, std::string requireTaskName, XMLNode* config){
    }
    AgentEntry* GetTaskProduction(std::string name){
        return NULL;
    }
};

class BaseBuildingSystemAgentEventContent {
};

class AgentEventContent: public BaseBuildingSystemAgentEventContent {
    std::string Name;
    XMLNode* content;
};

class ScopeRuntime {
};

class ScopeLauncher {
public:
    static void set_c_argv(int argc, char** argv){
    };    
    static int Launch(){
        return 0;
    };
};

int main(int argc, char** argv){
    ScopeLauncher::set_c_argv(argc, argv);
    return ScopeLauncher::Launch();
}

The following diagram describes Leap block production, as implemented in `libraries/chain/controller.cpp`:

```mermaid
flowchart TD
    pp[producer_plugin] --> D
    A("replay()"):::fun --> B("replay_push_block()"):::fun
    B --> E("maybe_switch_forks()"):::fun
    C("init()"):::fun ---> E
    C --> A
    D("push_block()"):::fun ---> E
    subgraph G["apply_block()"]
       direction TB
       start -- "stage = &Oslash;" --> sb
       sb("start_block()"):::fun -- "stage = building_block" --> et
       et["execute transactions" ] -- "stage = building_block" --> fb("finish_block()"):::fun
       fb -- "stage = assembled block" --> cb["add transaction metadata and create completed block"]
       cb -- "stage = completed block" --> commit("commit_block() (where we [maybe] add to fork_db and mark valid)"):::fun

    end
    B ----> start
    E --> G
    D --> F("log_irreversible()"):::fun
    commit -- "stage =  &Oslash;" -->  F
    F -- "if in irreversible mode" --> G

    classDef fun fill:#f96
```
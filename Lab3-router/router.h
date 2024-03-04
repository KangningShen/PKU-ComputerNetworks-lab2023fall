#include "router_prototype.h"
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <set>
#include <unordered_set>
#include <unordered_map>

using namespace std;

class Router : public RouterBase {
public:
    void router_init(int port_num, int external_port, char* external_addr, char* available_addr);
    int router(int in_port, char* packet);

    static int router_count;    // from 1, maximum 100
    static unordered_map<uint32_t, int> hosttoport;   // host -> router_id * 500 + port_id

    int router_id;  // from 1
    int dis[104][104];   // the minimum distance to other routers & from other routers(id!!!) router id & router id
    int c[404];    // the cost linked with port x
    int nxt[104];   // the next node in the minimum distance path to other routers(port!!!), router id to port num

    int porttoid[404]; // port number to router id, 0 if not used
    int idtoport[104]; // router id to porter number, 0 if no used

    uint32_t porttohost[404]; // port number to host ip address

    int my_port_num;
    int my_external_port;

    uint32_t base_addr;
    uint32_t end_addr;
    uint32_t extohost[256];
    unordered_map<uint32_t, uint32_t> hosttoex;

    unordered_set<uint32_t> blocked_addr;
     
    Router()
    {
        router_id = ++router_count;
        memset(dis, 0x3f, sizeof(dis));
        memset(c, 0x3f, sizeof(c));
        memset(nxt, 0, sizeof(nxt));
        memset(porttoid, 0, sizeof(porttoid));
        memset(idtoport, 0, sizeof(idtoport));
        memset(porttohost, 0, sizeof(porttohost));
    }

};
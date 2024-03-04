#include "router.h"

#define MAX_LENGTH 16384
#define HEADER_LENGTH 12
#define INF 0x3f3f3f3f
#define TYPE_DV 0x00
#define TYPE_DATA 0x01
#define TYPE_CONTROL 0x02

struct ip_header{
    uint32_t    src;
    uint32_t    dst;
    uint8_t     type;
    uint16_t    length;
};

int Router::router_count = 0;
unordered_map<uint32_t, int> Router::hosttoport;

RouterBase* create_router_object() {
    return new Router;
}

void Router::router_init(int port_num, int external_port, char* external_addr, char* available_addr) {
    /*
    Initialize the router.
    port_num: The number of ports on this router, bigger than 0.
    external_port: external port number of this router
                   if 0, not connected to the external network
    external_addr: address range of the external network connected to this router
                   eg: 177.0.0.0/24     if external_port == 0, external_addr = NULL
    available_addr: address range of available public network addresses for this router
                   eg: 177.0.0.0/24     if external_port == 0, available_addr = NULL
    */

    my_port_num = port_num;
    my_external_port = external_port;

    if (!my_external_port)
        return;

    uint32_t exaddr = 0;
    int pos = 0;

    for (int _ = 0; _ < 4; _++)
    {
        uint32_t tempnum = 0;
        while (external_addr[pos] > '9' || external_addr[pos] < '0')
            pos++;
        while (external_addr[pos] <= '9' && external_addr[pos] >= '0')
        {
            tempnum = tempnum * 10 + int(external_addr[pos]) - 48;
            pos ++;
        }
        exaddr = exaddr * 256 + tempnum;
    }
    int ex_mask = 0;
    while (external_addr[pos] > '9' || external_addr[pos] < '0')
        pos++;
    while (external_addr[pos] <= '9' && external_addr[pos] >= '0')
    {
        ex_mask = ex_mask * 10 + int(external_addr[pos]) - 48;
        pos ++;
    }
    ex_mask = (1 << 31) >> (ex_mask - 1);
    base_addr = ex_mask & exaddr;
    ex_mask = (-1) ^ ex_mask;
    end_addr = ex_mask | base_addr;
    
    for (uint32_t i = base_addr; i <= end_addr; i++)
        Router::hosttoport[i] = router_id * 500 + external_port;

    uint32_t avaddr = 0;
    pos = 0;

    for (int _ = 0; _ < 4; _++)
    {
        uint32_t tempnum = 0;
        while (available_addr[pos] > '9' || available_addr[pos] < '0')
            pos++;
        while (available_addr[pos] <= '9' && available_addr[pos] >= '0')
        {
            tempnum = tempnum * 10 + int(available_addr[pos]) - 48;
            pos ++;
        }
        avaddr = avaddr * 256 + tempnum;
    }
    ex_mask = 0;
    while (available_addr[pos] > '9' || available_addr[pos] < '0')
        pos++;
    while (available_addr[pos] <= '9' && available_addr[pos] >= '0')
    {
        ex_mask = ex_mask * 10 + int(available_addr[pos]) - 48;
        pos ++;
    }
    ex_mask = (1 << 31) >> (ex_mask - 1);
    base_addr = ex_mask & avaddr;
    ex_mask = (-1) ^ ex_mask;
    end_addr = ex_mask | base_addr;
    
    for (uint32_t i = base_addr; i <= end_addr; i++)
        extohost[i - base_addr] = 0;
    
    return;
}

int Router::router(int in_port, char* packet) {
    /*
    Receive and forward packets
    in_port: the in port of the packet
    packet: received packet, the new packet should be stored here before return.
            no bigger than 16384 bytes.

    Return:
    port number - the port number to which the returned packet should be forwarded
    special cases:
    0 - broadcast the new packet to all neighbors
    -1 - the router throws the packet and no packet to return
    1 - forward the packet to Controller, when there is no route with the target address
    */
    
    
    ip_header header;
    memcpy(&header, packet, HEADER_LENGTH);

    int pos = HEADER_LENGTH;

    if (header.type == TYPE_DV)
    {
        // payload: "router_id:dis[1],dis[2],dis[3]"  eg: "5:1,3,2,0,14615615615,5."
        int neighbor_id = 0;

        while (packet[pos] < '0' || packet[pos] > '9')
            pos++;
        while (packet[pos] >= '0' && packet[pos] <= '9')
        {
            neighbor_id = neighbor_id * 10 + int(packet[pos]) - 48;
            pos++;
        }
        //printf("TYPE_DV    router_id:%d in_port:%d neighbor_id:%d\n", router_id, in_port, neighbor_id);
        porttoid[in_port] = neighbor_id;
        idtoport[neighbor_id] = in_port;
        int num = 0;
        while (packet[pos] != '.')
        {
            num ++;
            dis[neighbor_id][num] = 0;
            while (packet[pos] < '0' || packet[pos] > '9')
                pos ++;
            while (packet[pos] <= '9' && packet[pos] >= '0')
            {
                dis[neighbor_id][num] = dis[neighbor_id][num] * 10 + int(packet[pos]) - 48;;
                pos++;
            }
        }
        for (int i = num + 1; i <= Router::router_count; i++)
            dis[neighbor_id][i] = INF;

        int last_dis[104];
        for (int i = 1; i <= Router::router_count; i++)
        {
            last_dis[i] = dis[router_id][i];
            dis[router_id][i] = c[idtoport[i]];
            nxt[i] = idtoport[i];
        }
        dis[router_id][router_id] = 0;

        string tempstr = to_string(router_id);
        pos = HEADER_LENGTH;
        for (int i = 0; i < tempstr.size(); i++)
        {
            packet[pos] = tempstr[i];
            pos ++;
        }
        packet[pos] = ':';
        pos ++;

        bool change_flag = 0;
        for (int y = 1; y <= Router::router_count; y++) // for each y in N
        {
            for (int v = 1; v <= Router::router_count; v++) // for each v next to x(router_id)
            {
                if (!idtoport[v])
                    continue;
                if (c[idtoport[v]] + dis[v][y] < dis[router_id][y])
                {
                    dis[router_id][y] = dis[v][y] + c[idtoport[v]];
                    nxt[y] = idtoport[v];
                }
            }
            
            tempstr = to_string(dis[router_id][y]);
            for (int i = 0; i < tempstr.size(); i++)
            {
                packet[pos] = tempstr[i];
                pos ++;
            }
            if (y == Router::router_count)
                packet[pos] = '.';
            else
                packet[pos] = ',';
            pos ++;
            if (dis[router_id][y] != last_dis[y])
                change_flag = 1;
        }
        header.length = pos - HEADER_LENGTH;
        memcpy(packet, &header, HEADER_LENGTH);

        if (change_flag)
            return 0;
        else
            return -1;
    }
    else if (header.type == TYPE_DATA)
    {
        uint32_t src = ntohl(header.src), dst = ntohl(header.dst);
        //printf("TYPE_DATA    src:%u dst:%u\n", src, dst);
        
        if (blocked_addr.find(src) != blocked_addr.end())
            return -1;

        if (in_port == my_external_port)    // from external port
        {
            if (dst < base_addr || dst > 255 + base_addr || extohost[dst - base_addr] == 0)
                return -1;
            dst = extohost[dst - base_addr];
            header.dst = htonl(dst);
            memcpy(packet, &header, HEADER_LENGTH);
        }

        if (Router::hosttoport.find(dst) == Router::hosttoport.end())
            return 1;
        
        int dst_router = Router::hosttoport[dst] / 500, dst_port = Router::hosttoport[dst] % 500;
        if (dst_router == router_id)
        {
            if (my_external_port == dst_port)
            {
                if (hosttoex.find(src) == hosttoex.end())
                {
                    for (uint32_t i = base_addr; i <= end_addr; i++)
                    {
                        if (extohost[i - base_addr] == 0)
                        {
                            // ex(i) & host(src)
                            extohost[i - base_addr] = src;
                            hosttoex[src] = i;
                            break;
                        }
                    }
                }
                if (hosttoex.find(src) == hosttoex.end())
                    return -1;
                src = hosttoex[src];
                header.src = htonl(src);
                memcpy(packet, &header, HEADER_LENGTH);

                if (blocked_addr.find(src) != blocked_addr.end())
                    return -1;
            }
            return dst_port;
        }
        else if (dis[router_id][dst_router] == INF)
        {
            return 1;
        }
        else
        {
            return nxt[dst_router];
        }
    }
    else if (header.type == TYPE_CONTROL)
    {
        char op = packet[HEADER_LENGTH];
        if (op == '0')  // TRIGGER DV SEND
        {
            //printf("TRIGGER    router_id:%d\n", router_id);
            int last_dis[104];
            for (int i = 1; i <= Router::router_count; i++)
            {
                last_dis[i] = dis[router_id][i];
                dis[router_id][i] = c[idtoport[i]];
                nxt[i] = idtoport[i];
            }
            dis[router_id][router_id] = 0;

            string tempstr = to_string(router_id);
            pos = HEADER_LENGTH;
            for (int i = 0; i < tempstr.size(); i++)
            {
                packet[pos] = tempstr[i];
                pos ++;
            }
            packet[pos] = ':';
            pos ++;

            bool change_flag = 0;
            for (int y = 1; y <= Router::router_count; y++) // for each y in N
            {
                for (int v = 1; v <= Router::router_count; v++) // for each v next to x(router_id)
                {
                    if (!idtoport[v])
                        continue;
                    if (c[idtoport[v]] + dis[v][y] < dis[router_id][y])
                    {
                        dis[router_id][y] = dis[v][y] + c[idtoport[v]];
                        nxt[y] = idtoport[v];
                    }
                }
                tempstr = to_string(dis[router_id][y]);
                for (int i = 0; i < tempstr.size(); i++)
                {
                    packet[pos] = tempstr[i];
                    pos ++;
                }
                if (y == Router::router_count)
                    packet[pos] = '.';
                else
                    packet[pos] = ',';
                pos ++;
                if (dis[router_id][y] != last_dis[y])
                    change_flag = 1;
            }
            header.length = pos - HEADER_LENGTH;
            header.type = TYPE_DV;
            memcpy(packet, &header, HEADER_LENGTH);

            if (change_flag)
                return 0;
            else
                return -1;
        }
        else if (op == '1') // RELEASE NAT ITEM
        {
            uint32_t internal_ip = 0;
            pos ++;
            for (int _ = 0; _ < 4; _++)
            {
                uint32_t tempnum = 0;
                while (packet[pos] > '9' || packet[pos] < '0')
                    pos++;
                while (pos < HEADER_LENGTH + header.length && packet[pos] <= '9' && packet[pos] >= '0')
                {
                    tempnum = tempnum * 10 + int(packet[pos]) - 48;
                    pos ++;
                }
                internal_ip = internal_ip * 256 + tempnum;
            }
            if (hosttoex.find(internal_ip) != hosttoex.end())
            {
                extohost[hosttoex[internal_ip] - base_addr] = 0;
                hosttoex.erase(internal_ip);
            }
            return -1;
        }
        else if (op == '2') // PORT VALUE CHANGE
        {
            pos++;
            int neighbor_port = 0, path_value = 0;
            while (packet[pos] == ' ')
                pos++;
            while (packet[pos] <= '9' && packet[pos] >= '0')
            {
                neighbor_port = neighbor_port * 10 + int(packet[pos]) - 48;
                pos ++;
            }
            while (packet[pos] == ' ')
                pos++;
            bool sign_num = 0;
            if (packet[pos] == '-')
            {
                sign_num = 1;
                pos ++;
            }
            while (packet[pos] <= '9' && packet[pos] >= '0')
            {
                path_value = path_value * 10 + int(packet[pos]) - 48;
                pos ++;
            }
            if (sign_num)
            {
                path_value *= -1;
                c[neighbor_port] = INF;
                if (porttohost[neighbor_port])
                {
                    Router::hosttoport.erase(porttohost[neighbor_port]);
                    porttohost[neighbor_port] = 0;
                    return -1;
                }
                else
                {
                    idtoport[porttoid[neighbor_port]] = 0;
                    porttoid[neighbor_port] = 0;
                }
            }
            else
            {
                c[neighbor_port] = path_value;
            }

            //printf("LINK    router_id:%d neighbor_port:%d path_value:%d\n", router_id, neighbor_port, path_value);

            pos = HEADER_LENGTH;
            string tempstr = to_string(router_id);
            for (int i = 0; i < tempstr.size(); i++)
            {
                packet[pos] = tempstr[i];
                pos ++;
            }
            packet[pos] = ':';
            pos ++;

            for (int y = 1; y <= Router::router_count; y++)
            {
                tempstr = to_string(dis[router_id][y]);
                for (int i = 0; i < tempstr.size(); i++)
                {
                    packet[pos] = tempstr[i];
                    pos ++;
                }
                if (y == Router::router_count)
                    packet[pos] = '.';
                else
                    packet[pos] = ',';
                pos ++;
            }
            header.length = pos - HEADER_LENGTH;
            header.type = TYPE_DV;
            memcpy(packet, &header, HEADER_LENGTH);
            return 0;
            
        }
        else if (op == '3') // ADD HOST
        {
            int host_port = 0;
            uint32_t host_ip = 0;

            pos ++;
            while (packet[pos] > '9' || packet[pos] < '0')
                pos++;
            while (packet[pos] <= '9' && packet[pos] >= '0')
            {
                host_port = host_port * 10 + int(packet[pos]) - 48;
                pos ++;
            }


            for (int _ = 0; _ < 4; _++)
            {
                uint32_t tempnum = 0;
                while (packet[pos] > '9' || packet[pos] < '0')
                    pos++;
                while (pos < HEADER_LENGTH + header.length && packet[pos] <= '9' && packet[pos] >= '0')
                {
                    tempnum = tempnum * 10 + int(packet[pos]) - 48;
                    pos ++;
                }
                host_ip = host_ip * 256 + tempnum;
            }

            porttohost[host_port] = host_ip;
            Router::hosttoport[host_ip] = 500 * router_id + host_port;

            //printf("ADDHOST    router_id:%d host_ip:%u host_port:%d\n", router_id, host_ip, host_port);
            return -1;
        }
        else if (op == '5') // BLOCK ADDR
        {
            uint32_t blocked_ip = 0;
            pos ++;
            for (int _ = 0; _ < 4; _++)
            {
                uint32_t tempnum = 0;
                while (packet[pos] > '9' || packet[pos] < '0')
                    pos++;
                while (pos < HEADER_LENGTH + header.length && packet[pos] <= '9' && packet[pos] >= '0')
                {
                    tempnum = tempnum * 10 + int(packet[pos]) - 48;
                    pos ++;
                }
                blocked_ip = blocked_ip * 256 + tempnum;
            }
            blocked_addr.insert(blocked_ip);
            return -1;
        }
        else if (op == '6') // UNBLOCK ADDR
        {
            uint32_t unblocked_ip = 0;
            pos ++;
            for (int _ = 0; _ < 4; _++)
            {
                uint32_t tempnum = 0;
                while (packet[pos] > '9' || packet[pos] < '0')
                    pos++;
                while (pos < HEADER_LENGTH + header.length && packet[pos] <= '9' && packet[pos] >= '0')
                {
                    tempnum = tempnum * 10 + int(packet[pos]) - 48;
                    pos ++;
                }
                unblocked_ip = unblocked_ip * 256 + tempnum;
            }
            if (blocked_addr.find(unblocked_ip) != blocked_addr.end())
                blocked_addr.erase(unblocked_ip);
            return -1;
        }
    }
    return 1;
}
#include <iostream>
#include <vector>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <queue>

#define MAX_NODES 100
#define INF 1e9

using namespace std;



vector<int> nodes;
map<int, vector<int>> next_hop;


void distance_vector(const map<int, map<int, int>>& graph, int num_nodes, ofstream& fpOut) {
    map<int, vector<vector<int>>> dis_vec;
    map<int, vector<int>> prev_map;
    for (const auto& node : graph) {
        dis_vec[node.first] = vector<vector<int>>(num_nodes + 1, vector<int>(num_nodes + 1, INF));
        dis_vec[node.first][node.first][node.first] = 0;
        next_hop[node.first] = vector<int>(num_nodes + 1, INF);
        prev_map[node.first] = vector<int>(num_nodes + 1, -1);
    }

    for (const auto& u_map : graph) {
        int u = u_map.first;
        for (const auto& edge : u_map.second) {
            int v = edge.first;
            int weight = edge.second;
            dis_vec[u][u][v] = weight;
            dis_vec[v][v][u] = weight;
            next_hop[u][v] = v;
            next_hop[v][u] = u;
            prev_map[u][v] = u;
            prev_map[v][u] = v;
        }
    }
    int flag = 1;
    int iteration = 0;
    while (flag ) {
        iteration ++;
        if (iteration == num_nodes) break;
        cout << "interation: " << iteration << endl;
        flag = 0;
        for (const auto& u_map : graph) {
            int u = u_map.first;
            for (const auto& v_map : graph) {
                int dest = v_map.first;
                for (const auto& edge : u_map.second) {
                    int neighbor = edge.first;
                    int weight = dis_vec[u][u][neighbor] + dis_vec[neighbor][neighbor][dest];
                    int hop = next_hop[u][neighbor];
                    if ((dis_vec[u][u][dest] > weight) 
                    || ((dis_vec[u][u][dest] == weight) && (prev_map[hop][dest] < prev_map[u][dest]) && (prev_map[hop][dest]!=-1)) ) {
                        flag = 1;
                        dis_vec[u][u][dest] = weight;
                        next_hop[u][dest] = hop;
                        prev_map[u][dest] = prev_map[hop][dest];
                        cout << "src: " << u << " dest: " << dest << " hop: " << hop << " cost: " << dis_vec[u][u][dest] 
                        << " prev: " << prev_map[hop][dest] << endl;
                    }
                }
            }
        }
    }
    cout << "-------------------------------------------------" << endl;
    // Write results to fpOut
    for (int src = 1; src <= num_nodes; src++) {
        for (int dest = 1; dest <= num_nodes; dest++) {
            if (src == dest) {
                fpOut << dest << " " << dest << " " << 0 << endl;
            } else if( dis_vec[src][src][dest] != INF) {
                int next_hop_node = next_hop[src][dest];
                int path_cost = dis_vec[src][src][dest];
                fpOut << dest << " " << next_hop_node << " " << path_cost << endl;
                cout << "Destination: " << dest << ", Next Hop: " << next_hop_node << ", Path Cost: " << path_cost << endl;
            }  else {
                cout << "Destination: " << dest << " is unreachable from source node " << src << endl;
            }
        }
        fpOut << endl;
    }
}


void output_message(const map<int, map<int, int>>& graph, char* arg2, ofstream& fpOut) {
    ifstream fpMessage(arg2);
    string line, message;
    int src, dest;
    vector<int> src_list, dest_list;
    vector<string> message_list;
    while (getline(fpMessage, line)) {
        istringstream iss(line);
        iss >> src >> dest;
        // Read the remaining content of the line as the message
        if (iss.ignore()) {
            getline(iss, message);
        } else {
            continue;
        }
        src_list.push_back(src);
        dest_list.push_back(dest);
        message_list.push_back(message);
    }
    fpMessage.close();
    for (int i=0; i<src_list.size(); i++) {
        src = src_list[i];
        dest = dest_list[i];
        message = message_list[i];
    
        if ((find(nodes.begin(), nodes.end(), src) == nodes.end()) || (find(nodes.begin(), nodes.end(), dest) == nodes.end())) {
            fpOut << "from " << src << " to " << dest << " cost infinite hops unreachable message " << message.c_str() << endl;
            continue;
        }
        
        int currentNode = src;
        int totalCost = 0;
        vector<int> path;
        while (currentNode != dest) {
            int nextNode = next_hop[currentNode][dest];
            if (nextNode == INF) {
                fpOut << "from " << src << " to " << dest << " cost infinite hops unreachable message " << message.c_str() << endl;
                fpOut << endl;
                break;
            }
            totalCost += graph.at(currentNode).at(nextNode);
            path.push_back(currentNode);
            currentNode = nextNode;
        }
        if (currentNode == dest) {
            // path.push_back(dest);

            fpOut << "from " << src << " to " << dest << " cost " << totalCost << " hops";
            for (int i = 0; i < path.size(); i++) {
                fpOut << " " << path[i];
            }
            fpOut << " message " << message.c_str() << "\n"; 
            fpOut << endl;
        }
    }
}


int main(int argc, char** argv) {
    if (argc != 4) {
        cout << "Usage: ./linkstate topofile messagefile changesfile" << '\n';
        return -1;
    }
    ofstream fpOut("output.txt");

    ifstream fpTopo(argv[1]);
    int src, dest, weight;
    
    map<int, map<int, int>> graph;
    while (fpTopo >> src >> dest >> weight) {
        graph[src][dest] = weight;
        graph[dest][src] = weight;
        if (find(nodes.begin(), nodes.end(), src) == nodes.end()) {
            nodes.push_back(src);
        }
        if (find(nodes.begin(), nodes.end(), dest) == nodes.end()) {
            nodes.push_back(dest);
        }
    }

    // sort(nodes.begin(), nodes.end());
    
    distance_vector(graph, nodes.size(), fpOut);

    fpTopo.close();

    output_message(graph, argv[2], fpOut);

    ifstream fpChange(argv[3]);
    while (fpChange >> src >> dest >> weight) {
        if (weight > 0) {
            graph[src][dest] = weight;
            graph[dest][src] = weight;
            if (find(nodes.begin(), nodes.end(), src) == nodes.end()) {
                nodes.push_back(src);
            }
            if (find(nodes.begin(), nodes.end(), dest) == nodes.end()) {
                nodes.push_back(dest);
            }
        }
        if (weight == -999) {
            graph[src].erase(dest);
            graph[dest].erase(src);
        }     
        distance_vector(graph, nodes.size(), fpOut);
        output_message(graph, argv[2], fpOut);
    }

    fpOut.close();
    fpChange.close();

    return 0;
}


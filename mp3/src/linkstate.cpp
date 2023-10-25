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

using namespace std;

map<int, vector<int>> prev_map;
vector<int> nodes;

bool isLineEmptyOrWhitespace(const string& line) {
    return line.find_first_not_of(" \t") == string::npos;
}

void displayGraph(const map<int, map<int, int>>& graph) {
    cout << "----------------------------------" << endl;
    for (const auto& node_pair : graph) {
        int u = node_pair.first;
        for (const auto& edge : node_pair.second) {
            int v = edge.first;
            int weight = edge.second;
            cout << "Node " << u << " is connected to Node " << v << " with weight " << weight << endl;
        }
    }
}


void dijkstra(const map<int, map<int, int>>& graph, int src, int num_nodes, FILE* fpOut) {
    vector<int> dist(num_nodes + 1, numeric_limits<int>::max());
    vector<int> prev(num_nodes + 1, -1);
    vector<bool> visited(num_nodes + 1, false);

    dist[src] = 0;
    using QueueNode = pair<int, int>;
    priority_queue<QueueNode, vector<QueueNode>, greater<QueueNode>> pq;
    pq.push({0, src});

    while (!pq.empty()) {
        int u = pq.top().second;
        pq.pop();

        if (visited[u]) {
            continue;
        }
        visited[u] = true;

        for (const auto& edge : graph.at(u)) {
            int v = edge.first;
            int weight = edge.second;

            if (dist[u] + weight < dist[v] || (dist[u] + weight == dist[v] && u < prev[v])) {
                dist[v] = dist[u] + weight;
                prev[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    for (int i = 1; i <= num_nodes; ++i) {
        if (i == src) {
            cout << "Destination: " << i << ", Next Hop: " << src << ", Path Cost: 0" << endl;
            fprintf(fpOut, "%d %d %d\n", i, src, 0);
        } else if (dist[i] != numeric_limits<int>::max()) {
            int next_hop = i;
            int backtrack = prev[i];
            while (backtrack != src) {
                next_hop = backtrack;
                backtrack = prev[backtrack];
            }
            cout << "Destination: " << i << ", Next Hop: " << next_hop << ", Path Cost: " << dist[i] << endl;
            fprintf(fpOut, "%d %d %d\n", i, next_hop, dist[i]);
        } else {
            cout << "Destination: " << i << " is unreachable from source node " << src << endl;
        }
    }
    fprintf(fpOut, "\n");
    prev_map[src] = prev;
}

void output_message(const map<int, map<int, int>>& graph, const map<int, vector<int>>& prev_map, char* arg2,  FILE* fpOut) {
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
        cout << src << " " << dest << endl;
        
        if ((find(nodes.begin(), nodes.end(), src) == nodes.end()) || (find(nodes.begin(), nodes.end(), dest) == nodes.end())) {
            fprintf(fpOut, "from %d to %d cost infinite hops unreachable message %s\n", src, dest, message.c_str());
            fprintf(fpOut, "\n");
            continue;
        } 

        vector<int> prev = prev_map.at(src);
        if (prev[dest] == -1) {
            fprintf(fpOut, "from %d to %d cost infinite hops unreachable message %s\n", src, dest, message.c_str());
            fprintf(fpOut, "\n");
        } else {
            int currentNode = dest;
            int totalCost = 0;
            vector<int> path;
            while (currentNode != src) {
                int previousNode = prev[currentNode];
                totalCost += graph.at(previousNode).at(currentNode);
                path.push_back(previousNode);
                currentNode = previousNode;
            }
            reverse(path.begin(), path.end());

            fprintf(fpOut, "from %d to %d cost %d hops", src, dest, totalCost);
            for (int i=0; i<path.size(); i++) {
                fprintf(fpOut, " %d", path[i]);
            }
            fprintf(fpOut, " message %s\n", message.c_str());
            fprintf(fpOut, "\n");
        }
    }
}


int main(int argc, char** argv) {
    if (argc != 4) {
        cout << "Usage: ./linkstate topofile messagefile changesfile" << endl;
        return -1;
    }
    FILE* fpOut = fopen("output.txt", "w");

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
    sort(nodes.begin(), nodes.end());
    displayGraph(graph);

    for (int i = 0; i < nodes.size(); i++) {
        cout << nodes[i] << endl;
        dijkstra(graph, nodes[i], nodes.size(), fpOut);
    }
    displayGraph(graph);
    fpTopo.close();

    output_message(graph, prev_map, argv[2], fpOut);     // print the message

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
            sort(nodes.begin(), nodes.end());  
        }
        if (weight == -999) {
            graph[src].erase(dest);
            graph[dest].erase(src);
        } 
        cout << src << dest << weight;
        displayGraph(graph);
        for (int i = 0; i < nodes.size(); i++) {
            cout << nodes[i] << endl;
            dijkstra(graph, nodes[i], nodes.size(), fpOut);
        }
        output_message(graph, prev_map, argv[2], fpOut); 
    }

    fclose(fpOut);
    fpChange.close();

    return 0;
}


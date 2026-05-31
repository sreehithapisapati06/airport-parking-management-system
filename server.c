#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_SPOTS 10

struct ParkingSpot {
    char id[32];
    char name[16];
    int is_reserved;
    char vehicle[32];
    char owner[64];
};

struct ParkingSpot spots[MAX_SPOTS];

// Initialize a clean, centralized list of server-tracked parking spots
void init_spots() {
    char *names[MAX_SPOTS] = {"A1-1", "A1-2", "A1-3", "A1-4", "B1-1", "B1-2", "B1-3", "B1-4", "E1-1", "E1-2"};
    for (int i = 0; i < MAX_SPOTS; i++) {
        sprintf(spots[i].id, "SPOT_%03d", i + 1);
        strcpy(spots[i].name, names[i]);
        spots[i].is_reserved = 0;
        strcpy(spots[i].vehicle, "");
        strcpy(spots[i].owner, "");
    }
}

void handle_request(int client_sock) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    
    int bytes_received = read(client_sock, buffer, sizeof(buffer) - 1);
    if (bytes_received < 0) {
        close(client_sock);
        return;
    }

    // 1. ROUTE: Serve the static frontend HTML file
    if (strncmp(buffer, "GET / ", 6) == 0 || strncmp(buffer, "GET /index.html", 15) == 0) {
        FILE *html_file = fopen("index1.html", "r");
        if (html_file == NULL) {
            char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            write(client_sock, not_found, strlen(not_found));
            close(client_sock);
            return;
        }

        char *header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
        write(client_sock, header, strlen(header));

        char file_buffer[1024];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), html_file)) > 0) {
            write(client_sock, file_buffer, bytes_read);
        }
        fclose(html_file);
    } 
    
    // 2. ROUTE: GET Live Backend Parking Spots in JSON format
    else if (strncmp(buffer, "GET /api/spots", 14) == 0) {
        char json_body[2048] = "[";
        for (int i = 0; i < MAX_SPOTS; i++) {
            char spot_json[256];
            sprintf(spot_json, "{\"id\":\"%s\",\"name\":\"%s\",\"status\":\"%s\"}",
                    spots[i].id, 
                    spots[i].name, 
                    spots[i].is_reserved ? "occupied" : "free");
            strcat(json_body, spot_json);
            if (i < MAX_SPOTS - 1) strcat(json_body, ",");
        }
        strcat(json_body, "]");

        char response_header[512];
        sprintf(response_header, 
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", 
                strlen(json_body));
                
        write(client_sock, response_header, strlen(response_header));
        write(client_sock, json_body, strlen(json_body));
    } 
    
    // 3. ROUTE: POST a real Reservation request to the memory stack
    else if (strncmp(buffer, "POST /api/reserve", 17) == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) body += 4;

        // Parse primitive payload attributes: spotId, vehicleNo, ownerName
        char target_id[32] = {0};
        char vehicle[32] = {0};
        char owner[64] = {0};

        char *p_id = strstr(body, "spotId=");
        char *p_veh = strstr(body, "&vehicleNo=");
        char *p_own = strstr(body, "&ownerName=");

        if (p_id && p_veh && p_own) {
            sscanf(p_id, "spotId=%[^&]", target_id);
            sscanf(p_veh, "&vehicleNo=%[^&]", vehicle);
            sscanf(p_own, "&ownerName=%s", owner);
        }

        int found_index = -1;
        for (int i = 0; i < MAX_SPOTS; i++) {
            if (strcmp(spots[i].id, target_id) == 0) {
                found_index = i;
                break;
            }
        }

        char json_res[256];
        if (found_index == -1) {
            sprintf(json_res, "{\"success\":false,\"message\":\"Spot not found.\"}");
        } else if (spots[found_index].is_reserved) {
            sprintf(json_res, "{\"success\":false,\"message\":\"Spot already taken!\"}");
        } else {
            spots[found_index].is_reserved = 1;
            strcpy(spots[found_index].vehicle, vehicle);
            strcpy(spots[found_index].owner, owner);
            sprintf(json_res, "{\"success\":true,\"message\":\"Reservation stored on C server!\"}");
        }

        char response_header[512];
        sprintf(response_header, 
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n", 
                strlen(json_res));
                
        write(client_sock, response_header, strlen(response_header));
        write(client_sock, json_res, strlen(json_res));
    } 
    
    // 4. ROUTE: Fallback Error Catching
    else {
        char *msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
        write(client_sock, msg, strlen(msg));
    }

    close(client_sock);
}

int main() {
    int server_fd, client_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    init_spots();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    listen(server_fd, 10);
    printf("Unified Smart Parking Server online at http://localhost:%d\n", PORT);

    while (1) {
        client_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_sock >= 0) {
            handle_request(client_sock);
        }
    }

    close(server_fd);
    return 0;
}
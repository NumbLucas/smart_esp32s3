#ifndef _UDP_SERVER_H_
#define _UDP_SERVER_H_

#include "esp_err.h"
#include "esp_log.h"
#include "esp_camera.h"

#define UDP_SERVICE_PORT 10000

static char* TAG = "[UDP SERVER]";

#include <net/if.h>
#include "esp_err.h"
static int udp_socket = -1;
esp_err_t create_udp_server() {

    
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "create socket Failed");
        return ESP_FAIL;
    }
    //指定连接的服务器IP地址和端口号
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_SERVICE_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr))
            < 0) {
        ESP_LOGE(TAG, "socket bind Failed");
        close(udp_socket);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Create Udp Server socket: %d, succeed port : %d \n", udp_socket, UDP_SERVICE_PORT);
    return ESP_OK;
}

void send_udp_data(uint8_t* data_ptr, size_t size)
{
    struct sockaddr_in dst_addr;
	//清空结构体
	//memset(&dst_addr,0,sizeof(dst_addr));
	bzero(&dst_addr,sizeof(dst_addr));
	dst_addr.sin_family = AF_INET;//协议
	//将主机字节序转换成网络字节序
	dst_addr.sin_port = htons(10000);//端口
	//将字符串"192.168.0.110" 转换成32位整形数据 赋值IP地址
	inet_pton(AF_INET,"1.1.1.1", &dst_addr.sin_addr.s_addr);

    if (udp_socket < 1)
    {
        ESP_LOGI(TAG, "udp_socket error \n");
        return;
    }
    ESP_LOGI(TAG, "udp_socket  :%d \n", udp_socket);
    int result = sendto(udp_socket, data_ptr, size, 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr));
    ESP_LOGI(TAG, "send udp data result: %d \n", result);
	
}

void handle_capture_data(camera_fb_t * fb)
{
    send_udp_data(fb->buf, 100);
    // ESP_LOGI(TAG, "handle_capture_data");
}

#endif
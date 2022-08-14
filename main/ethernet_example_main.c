/* Ethernet Basic Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"


#include "esp_http_client.h"


#if CONFIG_ETH_USE_SPI_ETHERNET
#include "driver/spi_master.h"
#endif // CONFIG_ETH_USE_SPI_ETHERNET

static const char *TAG = "eth_example";

#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
#define INIT_SPI_ETH_MODULE_CONFIG(eth_module_config, num)                                      \
    do {                                                                                        \
        eth_module_config[num].spi_cs_gpio = CONFIG_EXAMPLE_ETH_SPI_CS ##num## _GPIO;           \
        eth_module_config[num].int_gpio = CONFIG_EXAMPLE_ETH_SPI_INT ##num## _GPIO;             \
        eth_module_config[num].phy_reset_gpio = CONFIG_EXAMPLE_ETH_SPI_PHY_RST ##num## _GPIO;   \
        eth_module_config[num].phy_addr = CONFIG_EXAMPLE_ETH_SPI_PHY_ADDR ##num;                \
    } while(0)

typedef struct {
    uint8_t spi_cs_gpio;
    uint8_t int_gpio;
    int8_t phy_reset_gpio;
    uint8_t phy_addr;
}spi_eth_module_config_t;
#endif

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}


void http_test_task_proc(void* arg);


void app_main(void)
{
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    // Create new default instance of esp-netif for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;
    mac_config.smi_mdc_gpio_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#if CONFIG_EXAMPLE_ETH_PHY_IP101
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_RTL8201
    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_LAN87XX
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_DP83848
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_KSZ8041
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz8041(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_KSZ8081
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz8081(&phy_config);
#endif
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    /* attach Ethernet driver to TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
#endif //CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET

#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
    // Create instance(s) of esp-netif for SPI Ethernet(s)
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg_spi = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    esp_netif_t *eth_netif_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { NULL };
    char if_key_str[10];
    char if_desc_str[10];
    char num_str[3];
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        itoa(i, num_str, 10);
        strcat(strcpy(if_key_str, "ETH_SPI_"), num_str);
        strcat(strcpy(if_desc_str, "eth"), num_str);
        esp_netif_config.if_key = if_key_str;
        esp_netif_config.if_desc = if_desc_str;
        esp_netif_config.route_prio = 30 - i;
        eth_netif_spi[i] = esp_netif_new(&cfg_spi);
    }

    // Init MAC and PHY configs to default
    eth_mac_config_t mac_config_spi = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config_spi = ETH_PHY_DEFAULT_CONFIG();

    // Install GPIO ISR handler to be able to service SPI Eth modlues interrupts
    gpio_install_isr_service(0);

    // Init SPI bus
    spi_device_handle_t spi_handle[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { NULL };
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Init specific SPI Ethernet module configuration from Kconfig (CS GPIO, Interrupt GPIO, etc.)
    spi_eth_module_config_t spi_eth_module_config[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    INIT_SPI_ETH_MODULE_CONFIG(spi_eth_module_config, 0);

    // spi_eth_module_config[0].int_gpio = 35; // BH

#if CONFIG_EXAMPLE_SPI_ETHERNETS_NUM > 1
    INIT_SPI_ETH_MODULE_CONFIG(spi_eth_module_config, 1);
#endif

    // Configure SPI interface and Ethernet driver for specific SPI module
    esp_eth_mac_t *mac_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    esp_eth_phy_t *phy_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM];
    esp_eth_handle_t eth_handle_spi[CONFIG_EXAMPLE_SPI_ETHERNETS_NUM] = { NULL };
#if CONFIG_EXAMPLE_USE_KSZ8851SNL
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20
    };

    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        // Set SPI module Chip Select GPIO
        devcfg.spics_io_num = spi_eth_module_config[i].spi_cs_gpio;

        ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle[i]));
        // KSZ8851SNL ethernet driver is based on spi driver
        eth_ksz8851snl_config_t ksz8851snl_config = ETH_KSZ8851SNL_DEFAULT_CONFIG(spi_handle[i]);

        // Set remaining GPIO numbers and configuration used by the SPI module
        ksz8851snl_config.int_gpio_num = spi_eth_module_config[i].int_gpio;
        phy_config_spi.phy_addr = spi_eth_module_config[i].phy_addr;
        phy_config_spi.reset_gpio_num = spi_eth_module_config[i].phy_reset_gpio;

        mac_spi[i] = esp_eth_mac_new_ksz8851snl(&ksz8851snl_config, &mac_config_spi);
        phy_spi[i] = esp_eth_phy_new_ksz8851snl(&phy_config_spi);
    }
#elif CONFIG_EXAMPLE_USE_DM9051
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20
    };

    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        // Set SPI module Chip Select GPIO
        devcfg.spics_io_num = spi_eth_module_config[i].spi_cs_gpio;

        ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle[i]));
        // dm9051 ethernet driver is based on spi driver
        eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(spi_handle[i]);

        // Set remaining GPIO numbers and configuration used by the SPI module
        dm9051_config.int_gpio_num = spi_eth_module_config[i].int_gpio;
        phy_config_spi.phy_addr = spi_eth_module_config[i].phy_addr;
        phy_config_spi.reset_gpio_num = spi_eth_module_config[i].phy_reset_gpio;

        mac_spi[i] = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config_spi);
        phy_spi[i] = esp_eth_phy_new_dm9051(&phy_config_spi);
    }
#elif CONFIG_EXAMPLE_USE_W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
        .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20
    };

    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        // Set SPI module Chip Select GPIO
        devcfg.spics_io_num = spi_eth_module_config[i].spi_cs_gpio;

        ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle[i]));
        // w5500 ethernet driver is based on spi driver
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle[i]);

        // Set remaining GPIO numbers and configuration used by the SPI module
        w5500_config.int_gpio_num = spi_eth_module_config[i].int_gpio;
        phy_config_spi.phy_addr = spi_eth_module_config[i].phy_addr;
        phy_config_spi.reset_gpio_num = spi_eth_module_config[i].phy_reset_gpio;

        mac_spi[i] = esp_eth_mac_new_w5500(&w5500_config, &mac_config_spi);
        phy_spi[i] = esp_eth_phy_new_w5500(&phy_config_spi);
    }
#endif //CONFIG_EXAMPLE_USE_W5500

    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        esp_eth_config_t eth_config_spi = ETH_DEFAULT_CONFIG(mac_spi[i], phy_spi[i]);
        ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config_spi, &eth_handle_spi[i]));

        /* The SPI Ethernet module might not have a burned factory MAC address, we cat to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
        */
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle_spi[i], ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
            0x02, 0x00, 0x00, 0x12, 0x34, 0x56 + i
        }));

        // attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif_spi[i], esp_eth_new_netif_glue(eth_handle_spi[i])));
    }
#endif // CONFIG_ETH_USE_SPI_ETHERNET

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    /* start Ethernet driver state machine */
#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
#endif // CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
#if CONFIG_EXAMPLE_USE_SPI_ETHERNET
    for (int i = 0; i < CONFIG_EXAMPLE_SPI_ETHERNETS_NUM; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handle_spi[i]));
    }
#endif // CONFIG_EXAMPLE_USE_SPI_ETHERNET


    // HTTP Test Task 생성 
    xTaskCreate(http_test_task_proc,"http_test",8192, NULL, 2, NULL);
}

typedef void (*t_http_agent_resp_proc)(const char *url,int method,const char *req_param, const char *res_data,int res_data_leng,int error,void *caller_data);
void http_agent_get_request(const char *url, t_http_agent_resp_proc res_proc,void *caller_data);
void HTTPRespProc(const char*url,int method,const char *req_param,const char *res_data,int res_data_len,int error,void *caller_data);



//--------------------------------------------------------------------------
void http_test_task_proc(void* arg)
{
    while (true){
        ESP_LOGI(TAG, "HTTP Test");
        vTaskDelay(pdMS_TO_TICKS(5000));


        char *url = "http://httpbin.org/ip";
        ESP_LOGI(TAG,"OP Req URL : %s",url);

        http_agent_get_request(url, HTTPRespProc,NULL);
    }
}


void HTTPRespProc(const char*url,int method,const char *req_param,const char *res_data,int res_data_len,int error,void *caller_data)
{
    ESP_LOGI(TAG,"HTTP Response : %s\n",res_data);
}


// ============================================================

char *newStrCp(const char *str) 
{
    if (str == NULL)
        return NULL;

    char *result = (char *)malloc(strlen(str) + 1);
    strcpy(result,str);
    return result;
}


char *newStrCpLen(const char *str,int len)
{
    if (str == NULL || len == 0)
        return NULL;

    char *result = (char *)malloc(len + 1);
    strncpy(result,str,len);
    result[len] = 0;
    return result;
}


char *newMemCpy(const char *data,int len)
{
    if (data == NULL || len <= 0)
        return NULL;

    char *result = (char *)malloc(len);
    memcpy(result,data,len);
    return result;
}


void safeFree(void *mem)
{
    if (mem)
        free(mem);
}


/*
    =========  HTTP Request & Response data structure
*/




typedef struct{
    // request data   ,  Task 에서 사용
    int method;
    char *url;
    char *req_param;

    // response data
	char *resp_data;
	int resp_max_length;
	int resp_data_length;

    // response proc
    t_http_agent_resp_proc resp_proc;
    void *caller_data;

} t_HTTP_PROC_DATA;


void clean_http_data(t_HTTP_PROC_DATA *inst)
{
    safeFree(inst->url);
    safeFree(inst->req_param);
    safeFree(inst->resp_data);
    free(inst);
}


void copy_http_data(t_HTTP_PROC_DATA *inst, char *d,int len)
{
    if ((inst->resp_data_length + len) > inst->resp_max_length)
        ESP_LOGE(TAG, "buffer overrun");

    // ESP_LOGI(TAG, "copy Data : %s",d);

	memcpy(inst->resp_data + inst->resp_data_length,    d,len);
	inst->resp_data_length += len;

    // ESP_LOGI(TAG, "Acc Resp Data : %d bytes",inst->resp_data_length);
}

// chunk mode에 사용한다..  memory 가 모자라면 추가로 할당을 한다. 
void append_http_data(t_HTTP_PROC_DATA *inst, char *d,int len)
{
    int require_size = inst->resp_data_length + len + 1; // 필요한 전체 메모리 크기  
    char *new_buffer = (char *)malloc(require_size);
    new_buffer[require_size - 1] = 0; // 문자열 출력 가능하도록 


    // 이전의 데이터를 복사 
    memcpy(new_buffer,inst->resp_data , inst->resp_data_length);
    // 받은 데이터를 뒤에 복사 
    memcpy(new_buffer + inst->resp_data_length, d, len);

    safeFree(inst->resp_data); // 이전의 buffer data를 clear

    inst->resp_data = new_buffer;
    inst->resp_data_length += len;
    inst->resp_max_length = inst->resp_data_length;

    ESP_LOGI(TAG, "append_data : %d", inst->resp_data_length);
}




void alloc_http_resp_buffer(t_HTTP_PROC_DATA *inst,int buff_length)
{
    safeFree(inst->resp_data);

    if (buff_length != 0)
    {
        inst->resp_data = (char *)malloc(buff_length + 1); // buffer alloc 후 초기화 
        memset(inst->resp_data,0,buff_length + 1);
        inst->resp_max_length = buff_length;
        inst->resp_data_length = 0;

        ESP_LOGI(TAG, "alloc_http_resp_buffer   max_leng = %d, data_leng = %d ",inst->resp_max_length , inst->resp_data_length);

    } else
    {
        inst->resp_data = NULL;
        inst->resp_max_length = 0;
        inst->resp_data_length = 0;

        // ESP_LOGI(TAG, "alloc_http_resp_buffer INIT  max_leng = %d, data_leng = %d ",inst->resp_max_length , inst->resp_data_length);
    }
}





// ---------------------------------------------------------------------   http event handler  
esp_err_t _http_agent_event_proc_(esp_http_client_event_t *evt)
{
    t_HTTP_PROC_DATA *proc_data = (t_HTTP_PROC_DATA *) evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            // ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            break;
        case HTTP_EVENT_ON_DATA:        
            if (esp_http_client_is_chunked_response(evt->client)) { // chunked response
                // ESP_LOGI(TAG, "Chunk size : %d",evt->data_len );

                if (proc_data->resp_data == NULL) // data 수신 buffer alloc
                    ESP_LOGI(TAG, "Chunk Mode ......");

                append_http_data(proc_data,(char *)evt->data ,evt->data_len); 
            } 
            else  // non chunked mode
            {
                if (proc_data->resp_data == NULL) // data 수신 buffer alloc
                {
                    int content_length = esp_http_client_get_content_length(evt->client);
                    alloc_http_resp_buffer(proc_data, content_length);

                    ESP_LOGI(TAG, "NON Chunk MODE Length : %d",content_length );
                }

                copy_http_data(proc_data,(char *)evt->data ,evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            // ESP_LOGE(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}


t_HTTP_PROC_DATA *init_HTTP_PROC_DATA(const char *url,int method, const char *req_param, int buff_length)
{
	t_HTTP_PROC_DATA *newInst = (t_HTTP_PROC_DATA *)malloc(sizeof(t_HTTP_PROC_DATA));


    // method
    newInst->method = method;

    // strcpy(newInst->url, url);
    newInst->url = newStrCp(url);
    newInst->req_param = newStrCp(req_param);        


    newInst->resp_data = NULL; // 아래의 alloc_http_resp_buffer 함수를 호출하기 위해 resp_data를 초기화 
    alloc_http_resp_buffer(newInst,buff_length);

	return newInst;
}




void http_agent_task_proc(void *user_data){

    // -----------------------------------------------  config 
    t_HTTP_PROC_DATA *proc_data = (t_HTTP_PROC_DATA *) user_data;
    
    esp_http_client_config_t config = {};
        config.url = proc_data->url;
        config.event_handler = _http_agent_event_proc_;
        config.user_data = user_data;        
        config.disable_auto_redirect = true;


    // -----------  client init 
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_LOGI(TAG, "URL :  %s",proc_data->url);

    esp_http_client_set_method(client, (esp_http_client_method_t) proc_data->method);
    if (proc_data->method == HTTP_METHOD_POST) // POST Request
    {
        const char *header_key = "Content-Type";
        const char *header_value = "application/json";

        esp_http_client_set_header(client, header_key, header_value);
        esp_http_client_set_post_field(client, proc_data->req_param , strlen(proc_data->req_param));
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                proc_data->resp_data_length);

    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    t_http_agent_resp_proc resp_hanlder = proc_data->resp_proc;
    resp_hanlder(proc_data->url, proc_data->method, proc_data->req_param,
                 proc_data->resp_data,proc_data->resp_data_length,err,proc_data->caller_data);

    clean_http_data(proc_data);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}



void http_agent_get_request(const char *url, t_http_agent_resp_proc res_proc,void *caller_data)
{
    t_HTTP_PROC_DATA *proc_data = init_HTTP_PROC_DATA(url,HTTP_METHOD_GET,NULL,0);
    proc_data->resp_proc = res_proc;
    proc_data->caller_data = caller_data;

    xTaskCreate(&http_agent_task_proc, "HT_A_G", 8192, proc_data, 2, NULL);
}





















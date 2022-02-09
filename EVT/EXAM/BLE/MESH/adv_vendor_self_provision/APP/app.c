/********************************** (C) COPYRIGHT *******************************
 * File Name          : app.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2022/01/18
 * Description        :
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/******************************************************************************/
#include "CONFIG.h"
#include "MESH_LIB.h"
#include "app_vendor_model_srv.h"
#include "app.h"
#include "HAL.h"

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
#define ADV_TIMEOUT       K_MINUTES(10)

#define SELENCE_ADV_ON    0x01
#define SELENCE_ADV_OF    0x00

/*********************************************************************
 * GLOBAL TYPEDEFS
 */

static uint8_t MESH_MEM[1024 * 2] = {0};

extern const ble_mesh_cfg_t app_mesh_cfg;
extern const struct device  app_dev;

static uint8_t App_TaskID = 0; // Task ID for internal task/event processing

static uint16_t App_ProcessEvent(uint8_t task_id, uint16_t events);

static uint8_t dev_uuid[16] = {0}; // 此设备的UUID
uint8_t        MACAddr[6];         // 此设备的mac

static const uint8_t self_prov_net_key[16] = {
    0x00, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x00, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};

static const uint8_t self_prov_dev_key[16] = {
    0x00, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x00, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};

static const uint8_t self_prov_app_key[16] = {
    0x00, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x00, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};

const uint16_t self_prov_net_idx = 0x0000;      // 自配网所用的net key
const uint16_t self_prov_app_idx = 0x0001;      // 自配网所用的app key
const uint32_t self_prov_iv_index = 0x00000000; // 自配网的iv_index
const uint16_t self_prov_addr = 0x0F01;         // 自配网的自身主元素地址
const uint8_t  self_prov_flags = 0x00;          // 是否处于key更新状态，默认为否
const uint16_t vendor_sub_addr = 0xC001;        // 配置自定义模型的订阅group地址

#if(!CONFIG_BLE_MESH_PB_GATT)
NET_BUF_SIMPLE_DEFINE_STATIC(rx_buf, 65);
#endif /* !PB_GATT */

/*********************************************************************
 * LOCAL FUNCION
 */

static void cfg_srv_rsp_handler( const cfg_srv_status_t *val );
static void link_open(bt_mesh_prov_bearer_t bearer);
static void link_close(bt_mesh_prov_bearer_t bearer, uint8_t reason);
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index);
static void cfg_cli_rsp_handler(const cfg_cli_status_t *val);
static void vendor_model_srv_rsp_handler(const vendor_model_srv_status_t *val);
static void prov_reset(void);

static struct bt_mesh_cfg_srv cfg_srv = {
    .relay = BLE_MESH_RELAY_ENABLED,
    .beacon = BLE_MESH_BEACON_DISABLED,
#if(CONFIG_BLE_MESH_FRIEND)
    .frnd = BLE_MESH_FRIEND_ENABLED,
#endif
#if(CONFIG_BLE_MESH_PROXY)
    .gatt_proxy = BLE_MESH_GATT_PROXY_ENABLED,
#endif
    /* 默认TTL为3 */
    .default_ttl = 3,
    /* 底层发送数据重试7次，每次间隔10ms（不含内部随机数） */
    .net_transmit = BLE_MESH_TRANSMIT(7, 10),
    /* 底层转发数据重试7次，每次间隔10ms（不含内部随机数） */
    .relay_retransmit = BLE_MESH_TRANSMIT(7, 10),
    .handler = cfg_srv_rsp_handler,
};

/* Attention on */
void app_prov_attn_on(struct bt_mesh_model *model)
{
    APP_DBG("app_prov_attn_on");
}

/* Attention off */
void app_prov_attn_off(struct bt_mesh_model *model)
{
    APP_DBG("app_prov_attn_off");
}

const struct bt_mesh_health_srv_cb health_srv_cb = {
    .attn_on = app_prov_attn_on,
    .attn_off = app_prov_attn_off,
};

static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

BLE_MESH_HEALTH_PUB_DEFINE(health_pub, 8);

struct bt_mesh_cfg_cli cfg_cli = {
    .handler = cfg_cli_rsp_handler,
};

uint16_t cfg_srv_keys[CONFIG_MESH_MOD_KEY_COUNT_DEF] = {BLE_MESH_KEY_UNUSED};
uint16_t cfg_srv_groups[CONFIG_MESH_MOD_GROUP_COUNT_DEF] = {BLE_MESH_ADDR_UNASSIGNED};

uint16_t cfg_cli_keys[CONFIG_MESH_MOD_KEY_COUNT_DEF] = {BLE_MESH_KEY_UNUSED};
uint16_t cfg_cli_groups[CONFIG_MESH_MOD_GROUP_COUNT_DEF] = {BLE_MESH_ADDR_UNASSIGNED};

uint16_t health_srv_keys[CONFIG_MESH_MOD_KEY_COUNT_DEF] = {BLE_MESH_KEY_UNUSED};
uint16_t health_srv_groups[CONFIG_MESH_MOD_GROUP_COUNT_DEF] = {BLE_MESH_ADDR_UNASSIGNED};

// root模型加载
static struct bt_mesh_model root_models[] = {
    BLE_MESH_MODEL_CFG_SRV(cfg_srv_keys, cfg_srv_groups, &cfg_srv),
    BLE_MESH_MODEL_CFG_CLI(cfg_cli_keys, cfg_cli_groups, &cfg_cli),
    BLE_MESH_MODEL_HEALTH_SRV(health_srv_keys, health_srv_groups, &health_srv, &health_pub),
};

struct bt_mesh_vendor_model_srv vendor_model_srv = {
    .srv_tid.trans_tid = 0xFF,
    .handler = vendor_model_srv_rsp_handler,
};

uint16_t vnd_model_srv_keys[CONFIG_MESH_MOD_KEY_COUNT_DEF] = {BLE_MESH_KEY_UNUSED};
uint16_t vnd_model_srv_groups[CONFIG_MESH_MOD_GROUP_COUNT_DEF] = {BLE_MESH_ADDR_UNASSIGNED};

// 自定义模型加载
struct bt_mesh_model vnd_models[] = {
    BLE_MESH_MODEL_VND_CB(CID_WCH, BLE_MESH_MODEL_ID_WCH_SRV, vnd_model_srv_op, NULL, vnd_model_srv_keys,
                          vnd_model_srv_groups, &vendor_model_srv, &bt_mesh_vendor_model_srv_cb),
};

// 模型组成 elements
static struct bt_mesh_elem elements[] = {
    {
        /* Location Descriptor (GATT Bluetooth Namespace Descriptors) */
        .loc = (0),
        .model_count = ARRAY_SIZE(root_models),
        .models = (root_models),
        .vnd_model_count = ARRAY_SIZE(vnd_models),
        .vnd_models = (vnd_models),
    }
};

// elements 构成 Node Composition
const struct bt_mesh_comp app_comp = {
    .cid = 0x07D7, // WCH 公司id
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

// 配网参数和回调
static const struct bt_mesh_prov app_prov = {
    .uuid = dev_uuid,
    .link_open = link_open,
    .link_close = link_close,
    .complete = prov_complete,
    .reset = prov_reset,
};

// 配网者管理的节点，第0个为自己，第1，2依次为配网顺序的节点
node_t app_nodes[1] = {0};

/*********************************************************************
 * GLOBAL TYPEDEFS
 */

/*********************************************************************
 * @fn      prov_enable
 *
 * @brief   使能配网功能
 *
 * @return  none
 */
static void prov_enable(void)
{
    if(bt_mesh_is_provisioned())
    {
        return;
    }

    // Make sure we're scanning for provisioning inviations
    bt_mesh_scan_enable();
    // Enable unprovisioned beacon sending
    bt_mesh_beacon_enable();

    if(CONFIG_BLE_MESH_PB_GATT)
    {
        bt_mesh_proxy_prov_enable();
    }
}

/*********************************************************************
 * @fn      link_open
 *
 * @brief   配网时后的link打开回调
 *
 * @param   bearer  - 当前link是PB_ADV还是PB_GATT
 *
 * @return  none
 */
static void link_open(bt_mesh_prov_bearer_t bearer)
{
    APP_DBG("");
}

/*********************************************************************
 * @fn      link_close
 *
 * @brief   配网后的link关闭回调
 *
 * @param   bearer  - 当前link是PB_ADV还是PB_GATT
 * @param   reason  - link关闭原因
 *
 * @return  none
 */
static void link_close(bt_mesh_prov_bearer_t bearer, uint8_t reason)
{
    if(reason != CLOSE_REASON_SUCCESS)
        APP_DBG("reason %x", reason);
}

/*********************************************************************
 * @fn      node_work_handler
 *
 * @brief   node 任务到期执行，判断是否还有未配置完成的节点，调用节点配置函数
 *
 * @return  TRUE    继续执行配置节点
 *          FALSE   节点配置完成，停止任务
 */
static BOOL node_work_handler(void)
{
    node_t *node;

    node = app_nodes;
    if(!node)
    {
        APP_DBG("Unable find Unblocked Node");
        return FALSE;
    }

    if(node->retry_cnt-- == 0)
    {
        APP_DBG("Ran Out of Retransmit");
        goto unblock;
    }

    if(!node->cb->stage(node))
    {
        return FALSE;
    }

unblock:

    node->fixed = TRUE;
    return FALSE;
}

/*********************************************************************
 * @fn      node_cfg_process
 *
 * @brief   找一个空闲的节点，执行配置流程
 *
 * @param   node        - 空节点指针
 * @param   net_idx     - 网络key编号
 * @param   addr        - 网络地址
 * @param   num_elem    - 元素数量
 *
 * @return  node_t / NULL
 */
static node_t *node_cfg_process(node_t *node, uint16_t net_idx, uint16_t addr, uint8_t num_elem)
{
    node = app_nodes;
    node->net_idx = net_idx;
    node->node_addr = addr;
    node->elem_count = num_elem;
    tmos_set_event(App_TaskID, APP_NODE_EVT);
    return node;
}

/*********************************************************************
 * @fn      local_stage_set
 *
 * @brief   设置本地node配置的下一个阶段（即配置自身）
 *
 * @param   node        - 要配置的节点
 * @param   new_stage   - 下一个阶段
 *
 * @return  none
 */
static void local_stage_set(node_t *node, local_stage_t new_stage)
{
    node->retry_cnt = 1;
    node->stage.local = new_stage;
}

/*********************************************************************
 * @fn      local_rsp
 *
 * @brief   每执行一个本地节点配置流程的回调，设置下一个配置阶段
 *
 * @param   p1      - 要配置的本地node
 * @param   p2      - 当前的状态
 *
 * @return  none
 */
static void local_rsp(void *p1, const void *p2)
{
    node_t                 *node = p1;
    const cfg_cli_status_t *val = p2;

    switch(val->cfgHdr.opcode)
    {
        case OP_APP_KEY_ADD:
            APP_DBG("local Application Key Added");
            local_stage_set(node, LOCAL_MOD_BIND_SET);
            break;
        case OP_MOD_APP_BIND:
            APP_DBG("local vendor Model Binded");
            local_stage_set(node, LOCAL_MOD_SUB_SET);
            break;
        case OP_MOD_SUB_ADD:
            APP_DBG("local vendor Model Subscription Set");
            local_stage_set(node, LOCAL_CONFIGURATIONED);
            break;
        default:
            APP_DBG("Unknown Opcode (0x%04x)", val->cfgHdr.opcode);
            return;
    }
}

/*********************************************************************
 * @fn      local_stage
 *
 * @brief   本地节点配置，添加app key，并为自定义客户端绑定app key
 *
 * @param   p1      - 要配置的本地node
 *
 * @return  TRUE    配置发送失败
 *          FALSE   配置发送正常
 */
static BOOL local_stage(void *p1)
{
    int     err;
    BOOL    ret = FALSE;
    node_t *node = p1;

    switch(node->stage.local)
    {
        case LOCAL_APPKEY_ADD:
            err = bt_mesh_cfg_app_key_add(node->net_idx, node->node_addr, self_prov_net_idx, self_prov_app_idx, self_prov_app_key);
            if(err)
            {
                APP_DBG("Unable to adding Application key (err %d)", err);
                ret = 1;
            }
            break;

        case LOCAL_MOD_BIND_SET:
            err = bt_mesh_cfg_mod_app_bind_vnd(node->net_idx, node->node_addr, node->node_addr, self_prov_app_idx, BLE_MESH_MODEL_ID_WCH_SRV, CID_WCH);
            if(err)
            {
                APP_DBG("Unable to Binding vendor Model (err %d)", err);
                ret = 1;
            }
            break;

            // 设置模型订阅
        case LOCAL_MOD_SUB_SET:
            err = bt_mesh_cfg_mod_sub_add_vnd(node->net_idx, node->node_addr, node->node_addr, vendor_sub_addr, BLE_MESH_MODEL_ID_WCH_SRV, CID_WCH);
            if(err)
            {
                APP_DBG("Unable to Set vendor Model Subscription (err %d)", err);
                ret = TRUE;
            }
            break;

        default:
            ret = 1;
            break;
    }

    return ret;
}

static const cfg_cb_t local_cfg_cb = {
    local_rsp,
    local_stage,
};

/*********************************************************************
 * @fn      prov_complete
 *
 * @brief   配网完成回调，重新开始广播
 *
 * @param   net_idx     - 网络key的index
 * @param   addr        - link关闭原因网络地址
 * @param   flags       - 是否处于key refresh状态
 * @param   iv_index    - 当前网络iv的index
 *
 * @return  none
 */
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    int     err;
    node_t *node;

    APP_DBG("");

    err = bt_mesh_provisioner_enable(BLE_MESH_PROV_ADV);
    if(err)
    {
        APP_DBG("Unabled Enable Provisoner (err:%d)", err);
    }

    node = node_cfg_process(node, net_idx, addr, ARRAY_SIZE(elements));
    if(!node)
    {
        APP_DBG("Unable allocate node object");
        return;
    }

    node->cb = &local_cfg_cb;
    local_stage_set(node, LOCAL_APPKEY_ADD);
}

/*********************************************************************
 * @fn      prov_reset
 *
 * @brief   复位配网功能回调
 *
 * @param   none
 *
 * @return  none
 */
static void prov_reset(void)
{
    APP_DBG("");

    prov_enable();
}

/*********************************************************************
 * @fn      cfg_srv_rsp_handler
 *
 * @brief   config 模型服务回调
 *
 * @param   val     - 回调参数，包括命令类型、配置命令执行状态
 *
 * @return  none
 */
static void cfg_srv_rsp_handler( const cfg_srv_status_t *val )
{
    if(val->cfgHdr.status)
    {
        // 配置命令执行不成功
        APP_DBG("warning opcode 0x%02x", val->cfgHdr.opcode);
        return;
    }
    if(val->cfgHdr.opcode == OP_APP_KEY_ADD)
    {
        APP_DBG("App Key Added");
    }
    else if(val->cfgHdr.opcode == OP_MOD_APP_BIND)
    {
        APP_DBG("Vendor Model Binded");
    }
    else if(val->cfgHdr.opcode == OP_MOD_SUB_ADD)
    {
        APP_DBG("Vendor Model Subscription Set");
    }
    else
    {
        APP_DBG("Unknow opcode 0x%02x", val->cfgHdr.opcode);
    }
}

/*********************************************************************
 * @fn      cfg_cli_rsp_handler
 *
 * @brief   收到cfg命令的应答回调，此处例程只处理配置节点命令应答，
 *          如果超时则延迟1秒后再次执行配置节点流程
 *
 * @param   val     - 回调参数，包含命令类型和返回数据
 *
 * @return  none
 */
static void cfg_cli_rsp_handler(const cfg_cli_status_t *val)
{
    node_t *node;
    APP_DBG("");

    node = app_nodes;
    if(!node)
    {
        APP_DBG("Unable find Unblocked Node");
        return;
    }

    if(val->cfgHdr.status == 0xFF)
    {
        APP_DBG("Opcode 0x%04x, timeout", val->cfgHdr.opcode);
        goto end;
    }

    node->cb->rsp(node, val);

end:
    tmos_start_task(App_TaskID, APP_NODE_EVT, K_SECONDS(1));
}

/*********************************************************************
 * @fn      vendor_model_srv_rsp_handler
 *
 * @brief   自定义模型服务回调
 *
 * @param   val     - 回调参数，包括消息类型、数据内容、长度、来源地址
 *
 * @return  none
 */
static void vendor_model_srv_rsp_handler(const vendor_model_srv_status_t *val)
{
    if(val->vendor_model_srv_Hdr.status)
    {
        // 有应答数据传输 超时未收到应答
        APP_DBG("Timeout opcode 0x%02x", val->vendor_model_srv_Hdr.opcode);
        return;
    }
    if(val->vendor_model_srv_Hdr.opcode == OP_VENDOR_MESSAGE_TRANSPARENT_MSG)
    {
        // 收到透传数据
        APP_DBG("len %d, data 0x%02x from 0x%04x", val->vendor_model_srv_Event.trans.len,
                val->vendor_model_srv_Event.trans.pdata[0],
                val->vendor_model_srv_Event.trans.addr);
    }
    else if(val->vendor_model_srv_Hdr.opcode == OP_VENDOR_MESSAGE_TRANSPARENT_WRT)
    {
        // 收到write数据
        APP_DBG("len %d, data 0x%02x from 0x%04x", val->vendor_model_srv_Event.write.len,
                val->vendor_model_srv_Event.write.pdata[0],
                val->vendor_model_srv_Event.write.addr);
    }
    else if(val->vendor_model_srv_Hdr.opcode == OP_VENDOR_MESSAGE_TRANSPARENT_IND)
    {
        // 发送的indicate已收到应答
    }
    else
    {
        APP_DBG("Unknow opcode 0x%02x", val->vendor_model_srv_Hdr.opcode);
    }
}

/*********************************************************************
 * @fn      keyPress
 *
 * @brief   按键回调
 *
 * @param   keys    - 按键类型
 *
 * @return  none
 */
void keyPress(uint8_t keys)
{
    APP_DBG("%d", keys);

    switch(keys)
    {
        default:
        {
            uint8_t           status;
            struct send_param param = {
                .app_idx = vnd_models[0].keys[0], // 此消息使用的app key，如无特定则使用第0个key
                .addr = vendor_sub_addr,          // 此消息发往的目的地地址，例程为发往订阅地址，包括自己
                .trans_cnt = 0x01,                // 此消息的用户层发送次数
                .period = K_MSEC(400),            // 此消息重传的间隔，建议不小于(200+50*TTL)ms，若数据较大则建议加长
                .rand = (0),                      // 此消息发送的随机延迟
                .tid = vendor_srv_tid_get(),      // tid，每个独立消息递增循环，srv使用128~191
                .send_ttl = BLE_MESH_TTL_DEFAULT, // ttl，无特定则使用默认值
            };
            uint8_t data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
            //			status = vendor_message_srv_indicate(&param, data, 8);	// 调用自定义模型服务的有应答指示函数发送数据，默认超时2s
            status = vendor_message_srv_send_trans(&param, data, 8); // 或者调用自定义模型服务的透传函数发送数据，只发送，无应答机制
            if(status)
                APP_DBG("indicate failed %d", status);
            break;
        }
    }
}

/*********************************************************************
 * @fn      blemesh_on_sync
 *
 * @brief   同步mesh参数，启用对应功能，不建议修改
 *
 * @return  none
 */
void blemesh_on_sync(void)
{
    int        err;
    mem_info_t info;

    if(tmos_memcmp(VER_MESH_LIB, VER_MESH_FILE, strlen(VER_MESH_FILE)) == FALSE)
    {
        PRINT("head file error...\n");
        while(1);
    }

    info.base_addr = MESH_MEM;
    info.mem_len = ARRAY_SIZE(MESH_MEM);

#if(CONFIG_BLE_MESH_FRIEND)
    friend_init_register(bt_mesh_friend_init, friend_state);
#endif /* FRIEND */
#if(CONFIG_BLE_MESH_LOW_POWER)
    lpn_init_register(bt_mesh_lpn_init, lpn_state);
#endif /* LPN */

    GetMACAddress(MACAddr);
    tmos_memcpy(dev_uuid, MACAddr, 6);
    err = bt_mesh_cfg_set(&app_mesh_cfg, &app_dev, MACAddr, &info);
    if(err)
    {
        APP_DBG("Unable set configuration (err:%d)", err);
        return;
    }
    hal_rf_init();
    err = bt_mesh_comp_register(&app_comp);

#if(CONFIG_BLE_MESH_RELAY)
    bt_mesh_relay_init();
#endif /* RELAY  */
#if(CONFIG_BLE_MESH_PROXY || CONFIG_BLE_MESH_PB_GATT)
  #if(CONFIG_BLE_MESH_PROXY)
    bt_mesh_proxy_beacon_init_register((void *)bt_mesh_proxy_beacon_init);
    gatts_notify_register(bt_mesh_gatts_notify);
    proxy_gatt_enable_register(bt_mesh_proxy_gatt_enable);
  #endif /* PROXY  */
  #if(CONFIG_BLE_MESH_PB_GATT)
    proxy_prov_enable_register(bt_mesh_proxy_prov_enable);
  #endif /* PB_GATT  */

    bt_mesh_proxy_init();
#endif /* PROXY || PB-GATT */

#if(CONFIG_BLE_MESH_PROXY_CLI)
    bt_mesh_proxy_client_init(cli); //待添加
#endif                              /* PROXY_CLI */

    bt_mesh_prov_retransmit_init();
#if(!CONFIG_BLE_MESH_PB_GATT)
    adv_link_rx_buf_register(&rx_buf);
#endif /* !PB_GATT */
    err = bt_mesh_prov_init(&app_prov);

    bt_mesh_mod_init();
    bt_mesh_net_init();
    bt_mesh_trans_init();
    bt_mesh_beacon_init();

    bt_mesh_adv_init();

#if((CONFIG_BLE_MESH_PB_GATT) || (CONFIG_BLE_MESH_PROXY) || (CONFIG_BLE_MESH_OTA))
    bt_mesh_conn_adv_init();
#endif /* PROXY || PB-GATT || OTA */

#if(CONFIG_BLE_MESH_SETTINGS)
    bt_mesh_settings_init();
#endif /* SETTINGS */

#if(CONFIG_BLE_MESH_PROXY_CLI)
    bt_mesh_proxy_cli_adapt_init();
#endif /* PROXY_CLI */

#if((CONFIG_BLE_MESH_PROXY) || (CONFIG_BLE_MESH_PB_GATT) || \
    (CONFIG_BLE_MESH_PROXY_CLI) || (CONFIG_BLE_MESH_OTA))
    bt_mesh_adapt_init();
#endif /* PROXY || PB-GATT || PROXY_CLI || OTA */

    if(err)
    {
        APP_DBG("Initializing mesh failed (err %d)", err);
        return;
    }

    APP_DBG("Bluetooth initialized");

#if(CONFIG_BLE_MESH_SETTINGS)
    settings_load();
#endif /* SETTINGS */

    if(bt_mesh_is_provisioned())
    {
        APP_DBG("Mesh network restored from flash");
    }
    else
    {
        err = bt_mesh_provision(self_prov_net_key, self_prov_net_idx, self_prov_flags,
                                self_prov_iv_index, self_prov_addr, self_prov_dev_key);
        if(err)
        {
            APP_DBG("Self Privisioning (err %d)", err);
            return;
        }
    }

    APP_DBG("Mesh initialized");
}

/*********************************************************************
 * @fn      App_Init
 *
 * @brief   应用层初始化
 *
 * @return  none
 */
void App_Init()
{
    App_TaskID = TMOS_ProcessEventRegister(App_ProcessEvent);

    blemesh_on_sync();
    HAL_KeyInit();
    HalKeyConfig(keyPress);
    tmos_start_task(App_TaskID, APP_NODE_TEST_EVT, 1600);
}

/*********************************************************************
 * @fn      App_ProcessEvent
 *
 * @brief   应用层事件处理函数
 *
 * @param   task_id  - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
static uint16_t App_ProcessEvent(uint8_t task_id, uint16_t events)
{
    // 节点配置任务事件处理
    if(events & APP_NODE_EVT)
    {
        if(node_work_handler())
            return (events);
        else
            return (events ^ APP_NODE_EVT);
    }

    if(events & APP_NODE_TEST_EVT)
    {
        tmos_start_task(App_TaskID, APP_NODE_TEST_EVT, 2400);
        return (events ^ APP_NODE_TEST_EVT);
    }

    // Discard unknown events
    return 0;
}

/******************************** endfile @ main ******************************/

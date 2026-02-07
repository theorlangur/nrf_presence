#ifndef ZB_MEM_CFG_HPP_
#define ZB_MEM_CFG_HPP_

#define ZB_CONFIG_OVERALL_NETWORK_SIZE 100 
#define ZB_CONFIG_HIGH_TRAFFIC
#define ZB_CONFIG_APPLICATION_COMPLEX

#include <zb_mem_config_common.h>

#undef ZB_CONFIG_IOBUF_POOL_SIZE
#define ZB_CONFIG_IOBUF_POOL_SIZE 128
#undef ZB_CONFIG_SCHEDULER_Q_SIZE
#define ZB_CONFIG_SCHEDULER_Q_SIZE 64
#undef ZB_CONFIG_APS_DUPS_TABLE_SIZE
#define ZB_CONFIG_APS_DUPS_TABLE_SIZE 64
#define ZB_CONFIG_NWK_DISC_TABLE_SIZE 32U

/* Memory context definitions. */
#include <zb_mem_config_context.h>

#endif

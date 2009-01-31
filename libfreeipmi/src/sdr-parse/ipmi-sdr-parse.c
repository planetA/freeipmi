/* 
   Copyright (C) 2003-2009 FreeIPMI Core Team

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.  

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#if STDC_HEADERS
#include <string.h>
#endif /* STDC_HEADERS */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <assert.h>
#include <errno.h>

#include "freeipmi/sdr-parse/ipmi-sdr-parse.h"

#include "freeipmi/debug/ipmi-debug.h"
#include "freeipmi/record-format/ipmi-sdr-record-format.h"
#include "freeipmi/spec/ipmi-sensor-units-spec.h"
#include "freeipmi/spec/ipmi-event-reading-type-code-spec.h"
#include "freeipmi/util/ipmi-sensor-util.h"
#include "ipmi-sdr-parse-defs.h"

#include "libcommon/ipmi-err-wrappers.h"
#include "libcommon/ipmi-fiid-wrappers.h"

#include "freeipmi-portability.h"
#include "debug-util.h"

#define IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD                          0x0001
#define IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD                       0x0002
#define IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD                           0x0004
#define IPMI_SDR_PARSE_RECORD_TYPE_ENTITY_ASSOCIATION_RECORD                   0x0008
#define IPMI_SDR_PARSE_RECORD_TYPE_DEVICE_RELATIVE_ENTITY_ASSOCIATION_RECORD   0x0010
#define IPMI_SDR_PARSE_RECORD_TYPE_GENERIC_DEVICE_LOCATOR_RECORD               0x0020
#define IPMI_SDR_PARSE_RECORD_TYPE_FRU_DEVICE_LOCATOR_RECORD                   0x0040
#define IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD 0x0080
#define IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_CONFIRMATION_RECORD   0x0100
#define IPMI_SDR_PARSE_RECORD_TYPE_BMC_MESSAGE_CHANNEL_INFO_RECORD             0x0200
#define IPMI_SDR_PARSE_RECORD_TYPE_OEM_RECORD                                  0x0400

static char *ipmi_sdr_parse_errmsgs[] =
  {
    "success",
    "context null",
    "context invalid",
    "invalid parameters",
    "out of memory",
    "invalid sdr record",
    "incomplete sdr record",
    "cannot parse or calculate",
    "internal system error",
    "internal error",
    "errnum out of range",
    NULL
  };

ipmi_sdr_parse_ctx_t
ipmi_sdr_parse_ctx_create(ipmi_ctx_t ipmi_ctx, ipmi_sdr_cache_ctx_t sdr_cache_ctx)
{
  struct ipmi_sdr_parse_ctx *ctx = NULL;
  
  ERR_EINVAL_NULL_RETURN(ipmi_ctx);

  ERR_CLEANUP((ctx = (ipmi_sdr_parse_ctx_t)malloc(sizeof(struct ipmi_sdr_parse_ctx))));
  memset(ctx, '\0', sizeof(struct ipmi_sdr_parse_ctx));
  ctx->magic = IPMI_SDR_PARSE_MAGIC;
  ctx->flags = IPMI_SDR_PARSE_FLAGS_DEFAULT;

  return ctx;

 cleanup:
  if (ctx)
    free(ctx);
  return NULL;
}

void
ipmi_sdr_parse_ctx_destroy(ipmi_sdr_parse_ctx_t ctx)
{
  if (!ctx || ctx->magic != IPMI_SDR_PARSE_MAGIC)
    return;

  ctx->magic = ~IPMI_SDR_PARSE_MAGIC;
  free(ctx);
}

int 
ipmi_sdr_parse_ctx_errnum(ipmi_sdr_parse_ctx_t ctx)
{
  if (!ctx)
    return IPMI_SDR_PARSE_CTX_ERR_CONTEXT_NULL;
  else if (ctx->magic != IPMI_SDR_PARSE_MAGIC)
    return IPMI_SDR_PARSE_CTX_ERR_CONTEXT_INVALID;
  else
    return ctx->errnum;
}

char *
ipmi_sdr_parse_ctx_strerror(int errnum)
{
  if (errnum >= IPMI_SDR_PARSE_CTX_ERR_SUCCESS && errnum <= IPMI_SDR_PARSE_CTX_ERR_ERRNUMRANGE)
    return ipmi_sdr_parse_errmsgs[errnum];
  else
    return ipmi_sdr_parse_errmsgs[IPMI_SDR_PARSE_CTX_ERR_ERRNUMRANGE];
}

char *
ipmi_sdr_parse_ctx_errormsg(ipmi_sdr_parse_ctx_t ctx)
{
  return ipmi_sdr_parse_ctx_strerror(ipmi_sdr_parse_ctx_errnum(ctx));
}

int
ipmi_sdr_parse_ctx_get_flags(ipmi_sdr_parse_ctx_t ctx, unsigned int *flags)
{
  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(flags);

  *flags = ctx->flags;
  return 0;
}

int
ipmi_sdr_parse_ctx_set_flags(ipmi_sdr_parse_ctx_t ctx, unsigned int flags)
{
  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(!(flags & ~IPMI_SDR_PARSE_FLAGS_MASK));

  ctx->flags = flags;
  return 0;
}

int
ipmi_sdr_parse_record_id_and_type (ipmi_sdr_parse_ctx_t ctx,
                                   uint8_t *sdr_record,
                                   unsigned int sdr_record_len,
                                   uint16_t *record_id,
                                   uint8_t *record_type)
{
  fiid_obj_t obj_sdr_record_header = NULL;
  int32_t sdr_record_header_len;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  SDR_PARSE_FIID_TEMPLATE_LEN_BYTES(sdr_record_header_len, tmpl_sdr_record_header);

  if (sdr_record_len < sdr_record_header_len)
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INCOMPLETE_SDR_RECORD);
      goto cleanup;
    }

  SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record_header, tmpl_sdr_record_header);

  SDR_PARSE_FIID_OBJ_SET_ALL_CLEANUP(obj_sdr_record_header,
                                     sdr_record,
                                     sdr_record_header_len);

  if (record_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_header, "record_id", &val);
      *record_id = val;
    }
  
  if (record_type)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_header, "record_type", &val);
      *record_type = val;
    }
  
  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_header);
  return rv;
}

static fiid_obj_t
_sdr_record_get_common(ipmi_sdr_parse_ctx_t ctx,
                       uint8_t *sdr_record,
                       unsigned int sdr_record_len,
                       uint32_t acceptable_record_types)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint8_t record_type;

  assert(ctx);
  assert(ctx->magic == IPMI_SDR_PARSE_MAGIC);
  assert(sdr_record);
  assert(sdr_record_len);
  assert(acceptable_record_types);

  if (ipmi_sdr_parse_record_id_and_type (ctx,
                                         sdr_record,
                                         sdr_record_len,
                                         NULL,
                                         &record_type) < 0)
    goto cleanup;

  if (!(((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD)
         && record_type == IPMI_SDR_FORMAT_FULL_SENSOR_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD)
            && record_type == IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD)
            && record_type == IPMI_SDR_FORMAT_EVENT_ONLY_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_ENTITY_ASSOCIATION_RECORD)
            && record_type == IPMI_SDR_FORMAT_ENTITY_ASSOCIATION_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_DEVICE_RELATIVE_ENTITY_ASSOCIATION_RECORD)
            && record_type == IPMI_SDR_FORMAT_DEVICE_RELATIVE_ENTITY_ASSOCIATION_RECORD)

        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_GENERIC_DEVICE_LOCATOR_RECORD)
            && record_type == IPMI_SDR_FORMAT_GENERIC_DEVICE_LOCATOR_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_FRU_DEVICE_LOCATOR_RECORD)
            && record_type == IPMI_SDR_FORMAT_FRU_DEVICE_LOCATOR_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD)
            && record_type == IPMI_SDR_FORMAT_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_CONFIRMATION_RECORD)
            && record_type == IPMI_SDR_FORMAT_MANAGEMENT_CONTROLLER_CONFIRMATION_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_BMC_MESSAGE_CHANNEL_INFO_RECORD)
            && record_type == IPMI_SDR_FORMAT_BMC_MESSAGE_CHANNEL_INFO_RECORD)
        || ((acceptable_record_types & IPMI_SDR_PARSE_RECORD_TYPE_OEM_RECORD)
            && record_type == IPMI_SDR_FORMAT_OEM_RECORD)
        ))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD);
      goto cleanup;
    }
  
  if (record_type == IPMI_SDR_FORMAT_FULL_SENSOR_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_full_sensor_record);
  else if (record_type == IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_compact_sensor_record);
  else if (record_type == IPMI_SDR_FORMAT_EVENT_ONLY_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_event_only_record);
  else if (record_type == IPMI_SDR_FORMAT_ENTITY_ASSOCIATION_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_entity_association_record);
  else if (record_type == IPMI_SDR_FORMAT_DEVICE_RELATIVE_ENTITY_ASSOCIATION_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_device_relative_entity_association_record);
  else if (record_type == IPMI_SDR_FORMAT_GENERIC_DEVICE_LOCATOR_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_generic_device_locator_record);
  else if (record_type == IPMI_SDR_FORMAT_FRU_DEVICE_LOCATOR_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_fru_device_locator_record);
  else if (record_type == IPMI_SDR_FORMAT_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_management_controller_device_locator_record);
  else if (record_type == IPMI_SDR_FORMAT_MANAGEMENT_CONTROLLER_CONFIRMATION_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_management_controller_confirmation_record);
  else if (record_type == IPMI_SDR_FORMAT_BMC_MESSAGE_CHANNEL_INFO_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_bmc_message_channel_info_record);
  else if (record_type == IPMI_SDR_FORMAT_OEM_RECORD)
    SDR_PARSE_FIID_OBJ_CREATE_CLEANUP(obj_sdr_record, tmpl_sdr_oem_record);

  SDR_PARSE_FIID_OBJ_SET_ALL_CLEANUP(obj_sdr_record,
                                     sdr_record,
                                     sdr_record_len);

  return obj_sdr_record;

 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return NULL;
}

int
ipmi_sdr_parse_sensor_owner_id (ipmi_sdr_parse_ctx_t ctx,
                                uint8_t *sdr_record,
                                unsigned int sdr_record_len,
                                uint8_t *sensor_owner_id_type,
                                uint8_t *sensor_owner_id)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (sensor_owner_id_type)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "sensor_owner_id.type", &val);
      *sensor_owner_id_type = val;
    }

  if (sensor_owner_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "sensor_owner_id", &val);
      *sensor_owner_id = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_sensor_owner_lun (ipmi_sdr_parse_ctx_t ctx,
                                 uint8_t *sdr_record,
                                 unsigned int sdr_record_len,
                                 uint8_t *sensor_owner_lun,
                                 uint8_t *channel_number)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (sensor_owner_lun)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "sensor_owner_lun", &val);
      *sensor_owner_lun = val;
    }

  if (channel_number)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "channel_number", &val);
      *channel_number = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_sensor_number (ipmi_sdr_parse_ctx_t ctx,
                              uint8_t *sdr_record,
                              unsigned int sdr_record_len,
                              uint8_t *sensor_number)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (sensor_number)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "sensor_number", &val);
      *sensor_number = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_entity_id_instance_type (ipmi_sdr_parse_ctx_t ctx,
                                        uint8_t *sdr_record,
                                        unsigned int sdr_record_len,
                                        uint8_t *entity_id,
                                        uint8_t *entity_instance,
                                        uint8_t *entity_instance_type)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (entity_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "entity_id", &val);
      *entity_id = val;
    }
  if (entity_instance)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "entity_instance", &val);
      *entity_instance = val;
    }
  if (entity_instance_type)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "entity_instance.type", &val);
      *entity_instance_type = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_sensor_type (ipmi_sdr_parse_ctx_t ctx,
                            uint8_t *sdr_record,
                            unsigned int sdr_record_len,
                            uint8_t *sensor_type)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (sensor_type)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "sensor_type", &val);
      *sensor_type = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_event_reading_type_code (ipmi_sdr_parse_ctx_t ctx,
                                        uint8_t *sdr_record,
                                        unsigned int sdr_record_len,
                                        uint8_t *event_reading_type_code)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (event_reading_type_code)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "event_reading_type_code", &val);
      *event_reading_type_code = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_id_string (ipmi_sdr_parse_ctx_t ctx,
                          uint8_t *sdr_record,
                          unsigned int sdr_record_len,
                          char *id_string,
                          unsigned int id_string_len)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_EVENT_ONLY_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (id_string && id_string_len)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP_DATA(obj_sdr_record,
                                          "id_string",
                                          (uint8_t *)id_string,
                                          id_string_len);
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_sensor_unit (ipmi_sdr_parse_ctx_t ctx,
                            uint8_t *sdr_record,
                            unsigned int sdr_record_len,
                            uint8_t *sensor_unit)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (sensor_unit)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "sensor_unit2.base_unit", &val);
      
      if (!IPMI_SENSOR_UNIT_VALID(val))
        val = IPMI_SENSOR_UNIT_UNSPECIFIED;
      
      *sensor_unit = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_sensor_capabilities (ipmi_sdr_parse_ctx_t ctx,
                                    uint8_t *sdr_record,
                                    unsigned int sdr_record_len,
                                    uint8_t *event_message_control_support,
                                    uint8_t *threshold_access_support,
                                    uint8_t *hysteresis_support,
                                    uint8_t *auto_re_arm_support,
                                    uint8_t *entity_ignore_support)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (event_message_control_support)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record,
                                      "sensor_capabilities.event_message_control_support",
                                      &val);
      *event_message_control_support = val;
    }
  if (threshold_access_support)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record,
                                      "sensor_capabilities.threshold_access_support",
                                      &val);
      *threshold_access_support = val;
    }
  if (hysteresis_support)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record,
                                      "sensor_capabilities.hysteresis_support",
                                      &val);
      *hysteresis_support = val;
    }
  if (auto_re_arm_support)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record,
                                      "sensor_capabilities.auto_re_arm_support",
                                      &val);
      *auto_re_arm_support = val;
    }
  if (entity_ignore_support)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record,
                                      "sensor_capabilities.entity_ignore_support",
                                      &val);
      *entity_ignore_support = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_assertion_supported (ipmi_sdr_parse_ctx_t ctx,
                                    uint8_t *sdr_record,
                                    unsigned int sdr_record_len,
                                    uint8_t *event_state_0,
                                    uint8_t *event_state_1,
                                    uint8_t *event_state_2,
                                    uint8_t *event_state_3,
                                    uint8_t *event_state_4,
                                    uint8_t *event_state_5,
                                    uint8_t *event_state_6,
                                    uint8_t *event_state_7,
                                    uint8_t *event_state_8,
                                    uint8_t *event_state_9,
                                    uint8_t *event_state_10,
                                    uint8_t *event_state_11,
                                    uint8_t *event_state_12,
                                    uint8_t *event_state_13,
                                    uint8_t *event_state_14)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_discrete = NULL;
  uint32_t acceptable_record_types;
  uint8_t record_type;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_GENERIC(event_reading_type_code)
      && !IPMI_EVENT_READING_TYPE_CODE_IS_SENSOR_SPECIFIC(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  /* convert obj_sdr_record to appropriate format we care about */
  if (ipmi_sdr_parse_record_id_and_type (ctx,
                                         sdr_record,
                                         sdr_record_len,
                                         NULL,
                                         &record_type) < 0)
    goto cleanup;

  if (record_type == IPMI_SDR_FORMAT_FULL_SENSOR_RECORD)
    SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_discrete,
                                    obj_sdr_record,
                                    tmpl_sdr_full_sensor_record_non_threshold_based_sensors);
  else
    SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_discrete,
                                    obj_sdr_record,
                                    tmpl_sdr_compact_sensor_record_non_threshold_based_sensors);

  if (event_state_0)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_0",
                                     &val);
      *event_state_0 = val;
    }

  if (event_state_1)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_1",
                                     &val);
      *event_state_1 = val;
    }

  if (event_state_2)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_2",
                                     &val);
      *event_state_2 = val;
    }

  if (event_state_3)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_3",
                                     &val);
      *event_state_3 = val;
    }

  if (event_state_4)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_4",
                                     &val);
      *event_state_4 = val;
    }

  if (event_state_5)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_5",
                                     &val);
      *event_state_5 = val;
    }

  if (event_state_6)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_6",
                                     &val);
      *event_state_6 = val;
    }

  if (event_state_7)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_7",
                                     &val);
      *event_state_7 = val;
    }

  if (event_state_8)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_8",
                                     &val);
      *event_state_8 = val;
    }

  if (event_state_9)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_9",
                                     &val);
      *event_state_9 = val;
    }

  if (event_state_10)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_10",
                                     &val);
      *event_state_10 = val;
    }

  if (event_state_11)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_11",
                                     &val);
      *event_state_11 = val;
    }

  if (event_state_12)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_12",
                                     &val);
      *event_state_12 = val;
    }

  if (event_state_13)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_13",
                                     &val);
      *event_state_13 = val;
    }

  if (event_state_14)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "assertion_event_mask.event_offset_14",
                                     &val);
      *event_state_14 = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_discrete);
  return rv;
}

int
ipmi_sdr_parse_deassertion_supported (ipmi_sdr_parse_ctx_t ctx,
                                      uint8_t *sdr_record,
                                      unsigned int sdr_record_len,
                                      uint8_t *event_state_0,
                                      uint8_t *event_state_1,
                                      uint8_t *event_state_2,
                                      uint8_t *event_state_3,
                                      uint8_t *event_state_4,
                                      uint8_t *event_state_5,
                                      uint8_t *event_state_6,
                                      uint8_t *event_state_7,
                                      uint8_t *event_state_8,
                                      uint8_t *event_state_9,
                                      uint8_t *event_state_10,
                                      uint8_t *event_state_11,
                                      uint8_t *event_state_12,
                                      uint8_t *event_state_13,
                                      uint8_t *event_state_14)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_discrete = NULL;
  uint32_t acceptable_record_types;
  uint8_t record_type;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_GENERIC(event_reading_type_code)
      && !IPMI_EVENT_READING_TYPE_CODE_IS_SENSOR_SPECIFIC(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  /* convert obj_sdr_record to appropriate format we care about */
  if (ipmi_sdr_parse_record_id_and_type (ctx,
                                         sdr_record,
                                         sdr_record_len,
                                         NULL,
                                         &record_type) < 0)
    goto cleanup;

  if (record_type == IPMI_SDR_FORMAT_FULL_SENSOR_RECORD)
    SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_discrete,
                                    obj_sdr_record,
                                    tmpl_sdr_full_sensor_record_non_threshold_based_sensors);
  else
    SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_discrete,
                                    obj_sdr_record,
                                    tmpl_sdr_compact_sensor_record_non_threshold_based_sensors);

  if (event_state_0)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_0",
                                     &val);
      *event_state_0 = val;
    }

  if (event_state_1)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_1",
                                     &val);
      *event_state_1 = val;
    }

  if (event_state_2)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_2",
                                     &val);
      *event_state_2 = val;
    }

  if (event_state_3)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_3",
                                     &val);
      *event_state_3 = val;
    }

  if (event_state_4)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_4",
                                     &val);
      *event_state_4 = val;
    }

  if (event_state_5)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_5",
                                     &val);
      *event_state_5 = val;
    }

  if (event_state_6)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_6",
                                     &val);
      *event_state_6 = val;
    }

  if (event_state_7)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_7",
                                     &val);
      *event_state_7 = val;
    }

  if (event_state_8)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_8",
                                     &val);
      *event_state_8 = val;
    }

  if (event_state_9)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_9",
                                     &val);
      *event_state_9 = val;
    }

  if (event_state_10)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_10",
                                     &val);
      *event_state_10 = val;
    }

  if (event_state_11)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_11",
                                     &val);
      *event_state_11 = val;
    }

  if (event_state_12)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_12",
                                     &val);
      *event_state_12 = val;
    }

  if (event_state_13)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_13",
                                     &val);
      *event_state_13 = val;
    }

  if (event_state_14)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_discrete,
                                     "deassertion_event_mask.event_offset_14",
                                     &val);
      *event_state_14 = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_discrete);
  return rv;
}

int
ipmi_sdr_parse_threshold_assertion_supported (ipmi_sdr_parse_ctx_t ctx,
                                              uint8_t *sdr_record,
                                              unsigned int sdr_record_len,
                                              uint8_t *lower_non_critical_going_low,
                                              uint8_t *lower_non_critical_going_high,
                                              uint8_t *lower_critical_going_low,
                                              uint8_t *lower_critical_going_high,
                                              uint8_t *lower_non_recoverable_going_low,
                                              uint8_t *lower_non_recoverable_going_high,
                                              uint8_t *upper_non_critical_going_low,
                                              uint8_t *upper_non_critical_going_high,
                                              uint8_t *upper_critical_going_low,
                                              uint8_t *upper_critical_going_high,
                                              uint8_t *upper_non_recoverable_going_low,
                                              uint8_t *upper_non_recoverable_going_high)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_threshold = NULL;
  uint32_t acceptable_record_types;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  /* achu:
   *
   * Technically, the IPMI spec lists that compact record formats also
   * support settable thresholds.  However, since compact records
   * don't contain any information for interpreting threshold sensors
   * (i.e. R exponent) I don't know how they could be of any use.  No
   * vendor that I know of supports threshold sensors via a compact
   * record (excluding possible OEM ones).
   *
   * There's a part of me that believes the readable/setting
   * threshold masks for compact sensor records is a cut and paste
   * typo.  It shouldn't be there.
   */

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  /* We don't want the generic sdr full record, we need the special
   * threshold one.
   */

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_THRESHOLD(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_threshold,
                                  obj_sdr_record,
                                  tmpl_sdr_full_sensor_record_threshold_based_sensors);

  if (lower_non_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.lower_non_critical_going_low_supported",
                                     &val);
      *lower_non_critical_going_low = val;
    }

  if (lower_non_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.lower_non_critical_going_high_supported",
                                     &val);
      *lower_non_critical_going_high = val;
    }

  if (lower_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.lower_critical_going_low_supported",
                                     &val);
      *lower_critical_going_low = val;
    }

  if (lower_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.lower_critical_going_high_supported",
                                     &val);
      *lower_critical_going_high = val;
    }

  if (lower_non_recoverable_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.lower_non_recoverable_going_low_supported",
                                     &val);
      *lower_non_recoverable_going_low = val;
    }

  if (lower_non_recoverable_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.lower_non_recoverable_going_high_supported",
                                     &val);
      *lower_non_recoverable_going_high = val;
    }

  if (upper_non_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.upper_non_critical_going_low_supported",
                                     &val);
      *upper_non_critical_going_low = val;
    }

  if (upper_non_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.upper_non_critical_going_high_supported",
                                     &val);
      *upper_non_critical_going_high = val;
    }

  if (upper_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.upper_critical_going_low_supported",
                                     &val);
      *upper_critical_going_low = val;
    }

  if (upper_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.upper_critical_going_high_supported",
                                     &val);
      *upper_critical_going_high = val;
    }

  if (upper_non_recoverable_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.upper_non_recoverable_going_low_supported",
                                     &val);
      *upper_non_recoverable_going_low = val;
    }

  if (upper_non_recoverable_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_assertion_event_mask.upper_non_recoverable_going_high_supported",
                                     &val);
      *upper_non_recoverable_going_high = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_threshold);
  return rv;
}

int
ipmi_sdr_parse_threshold_deassertion_supported (ipmi_sdr_parse_ctx_t ctx,
                                                uint8_t *sdr_record,
                                                unsigned int sdr_record_len,
                                                uint8_t *lower_non_critical_going_low,
                                                uint8_t *lower_non_critical_going_high,
                                                uint8_t *lower_critical_going_low,
                                                uint8_t *lower_critical_going_high,
                                                uint8_t *lower_non_recoverable_going_low,
                                                uint8_t *lower_non_recoverable_going_high,
                                                uint8_t *upper_non_critical_going_low,
                                                uint8_t *upper_non_critical_going_high,
                                                uint8_t *upper_critical_going_low,
                                                uint8_t *upper_critical_going_high,
                                                uint8_t *upper_non_recoverable_going_low,
                                                uint8_t *upper_non_recoverable_going_high)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_threshold = NULL;
  uint32_t acceptable_record_types;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  /* achu:
   *
   * Technically, the IPMI spec lists that compact record formats also
   * support settable thresholds.  However, since compact records
   * don't contain any information for interpreting threshold sensors
   * (i.e. R exponent) I don't know how they could be of any use.  No
   * vendor that I know of supports threshold sensors via a compact
   * record (excluding possible OEM ones).
   *
   * There's a part of me that believes the readable/setting
   * threshold masks for compact sensor records is a cut and paste
   * typo.  It shouldn't be there.
   */

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  /* We don't want the generic sdr full record, we need the special
   * threshold one.
   */

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_THRESHOLD(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_threshold,
                                  obj_sdr_record,
                                  tmpl_sdr_full_sensor_record_threshold_based_sensors);

  if (lower_non_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.lower_non_critical_going_low_supported",
                                     &val);
      *lower_non_critical_going_low = val;
    }

  if (lower_non_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.lower_non_critical_going_high_supported",
                                     &val);
      *lower_non_critical_going_high = val;
    }

  if (lower_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.lower_critical_going_low_supported",
                                     &val);
      *lower_critical_going_low = val;
    }

  if (lower_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.lower_critical_going_high_supported",
                                     &val);
      *lower_critical_going_high = val;
    }

  if (lower_non_recoverable_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.lower_non_recoverable_going_low_supported",
                                     &val);
      *lower_non_recoverable_going_low = val;
    }

  if (lower_non_recoverable_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.lower_non_recoverable_going_high_supported",
                                     &val);
      *lower_non_recoverable_going_high = val;
    }

  if (upper_non_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.upper_non_critical_going_low_supported",
                                     &val);
      *upper_non_critical_going_low = val;
    }

  if (upper_non_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.upper_non_critical_going_high_supported",
                                     &val);
      *upper_non_critical_going_high = val;
    }

  if (upper_critical_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.upper_critical_going_low_supported",
                                     &val);
      *upper_critical_going_low = val;
    }

  if (upper_critical_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.upper_critical_going_high_supported",
                                     &val);
      *upper_critical_going_high = val;
    }

  if (upper_non_recoverable_going_low)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.upper_non_recoverable_going_low_supported",
                                     &val);
      *upper_non_recoverable_going_low = val;
    }

  if (upper_non_recoverable_going_high)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "threshold_deassertion_event_mask.upper_non_recoverable_going_high_supported",
                                     &val);
      *upper_non_recoverable_going_high = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_threshold);
  return rv;
}

int
ipmi_sdr_parse_threshold_readable (ipmi_sdr_parse_ctx_t ctx,
                                   uint8_t *sdr_record,
                                   unsigned int sdr_record_len,
                                   uint8_t *lower_non_critical_threshold,
                                   uint8_t *lower_critical_threshold,
                                   uint8_t *lower_non_recoverable_threshold,
                                   uint8_t *upper_non_critical_threshold,
                                   uint8_t *upper_critical_threshold,
                                   uint8_t *upper_non_recoverable_threshold)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_threshold = NULL;
  uint32_t acceptable_record_types;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  /* achu:
   *
   * Technically, the IPMI spec lists that compact record formats also
   * support settable thresholds.  However, since compact records
   * don't contain any information for interpreting threshold sensors
   * (i.e. R exponent) I don't know how they could be of any use.  No
   * vendor that I know of supports threshold sensors via a compact
   * record (excluding possible OEM ones).
   *
   * There's a part of me that believes the readable/setting
   * threshold masks for compact sensor records is a cut and paste
   * typo.  It shouldn't be there.
   */

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  /* We don't want the generic sdr full record, we need the special
   * threshold one.
   */

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_THRESHOLD(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_threshold,
                                  obj_sdr_record,
                                  tmpl_sdr_full_sensor_record_threshold_based_sensors);

  if (lower_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "readable_threshold_mask.lower_non_critical_threshold_is_readable",
                                     &val);
      *lower_non_critical_threshold = val;
    }

  if (lower_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "readable_threshold_mask.lower_critical_threshold_is_readable",
                                     &val);
      *lower_critical_threshold = val;
    }

  if (lower_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "readable_threshold_mask.lower_non_recoverable_threshold_is_readable",
                                     &val);
      *lower_non_recoverable_threshold = val;
    }

  if (upper_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "readable_threshold_mask.upper_non_critical_threshold_is_readable",
                                     &val);
      *upper_non_critical_threshold = val;
    }

  if (upper_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "readable_threshold_mask.upper_critical_threshold_is_readable",
                                     &val);
      *upper_critical_threshold = val;
    }

  if (upper_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "readable_threshold_mask.upper_non_recoverable_threshold_is_readable",
                                     &val);
      *upper_non_recoverable_threshold = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_threshold);
  return rv;
}

int
ipmi_sdr_parse_threshold_settable (ipmi_sdr_parse_ctx_t ctx,
                                   uint8_t *sdr_record,
                                   unsigned int sdr_record_len,
                                   uint8_t *lower_non_critical_threshold,
                                   uint8_t *lower_critical_threshold,
                                   uint8_t *lower_non_recoverable_threshold,
                                   uint8_t *upper_non_critical_threshold,
                                   uint8_t *upper_critical_threshold,
                                   uint8_t *upper_non_recoverable_threshold)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_threshold = NULL;
  uint32_t acceptable_record_types;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  /* achu:
   *
   * Technically, the IPMI spec lists that compact record formats also
   * support settable thresholds.  However, since compact records
   * don't contain any information for interpreting threshold sensors
   * (i.e. R exponent) I don't know how they could be of any use.  No
   * vendor that I know of supports threshold sensors via a compact
   * record (excluding possible OEM ones).
   *
   * There's a part of me that believes the readable/setting
   * threshold masks for compact sensor records is a cut and paste
   * typo.  It shouldn't be there.
   */

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  /* We don't want the generic sdr full record, we need the special
   * threshold one.
   */

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_THRESHOLD(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_threshold,
                                  obj_sdr_record,
                                  tmpl_sdr_full_sensor_record_threshold_based_sensors);

  if (lower_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "settable_threshold_mask.lower_non_critical_threshold_is_settable",
                                     &val);
      *lower_non_critical_threshold = val;
    }

  if (lower_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "settable_threshold_mask.lower_critical_threshold_is_settable",
                                     &val);
      *lower_critical_threshold = val;
    }

  if (lower_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "settable_threshold_mask.lower_non_recoverable_threshold_is_settable",
                                     &val);
      *lower_non_recoverable_threshold = val;
    }

  if (upper_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "settable_threshold_mask.upper_non_critical_threshold_is_settable",
                                     &val);
      *upper_non_critical_threshold = val;
    }

  if (upper_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "settable_threshold_mask.upper_critical_threshold_is_settable",
                                     &val);
      *upper_critical_threshold = val;
    }

  if (upper_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "settable_threshold_mask.upper_non_recoverable_threshold_is_settable",
                                     &val);
      *upper_non_recoverable_threshold = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_threshold);
  return rv;
}

int
ipmi_sdr_parse_sensor_decoding_data (ipmi_sdr_parse_ctx_t ctx,
                                     uint8_t *sdr_record,
                                     unsigned int sdr_record_len,
                                     int8_t *r_exponent,
                                     int8_t *b_exponent,
                                     int16_t *m,
                                     int16_t *b,
                                     uint8_t *linearization,
                                     uint8_t *analog_data_format)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val, val1, val2;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (r_exponent)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "r_exponent", &val);
      *r_exponent = (int8_t) val;
      if (*r_exponent & 0x08)
        *r_exponent |= 0xF0;
    }

  if (b_exponent)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "b_exponent", &val);
      *b_exponent = (int8_t) val;
      if (*b_exponent & 0x08)
        *b_exponent |= 0xF0;
    }

  if (m)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "m_ls", &val1);
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "m_ms", &val2);
      *m = (int16_t)val1;
      *m |= ((val2 & 0x3) << 8);
      if (*m & 0x200)
        *m |= 0xFE00;
    }

  if (b)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "b_ls", &val1);
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "b_ms", &val2);
      *b = (int16_t)val1;
      *b |= ((val2 & 0x3) << 8);
      if (*b & 0x200)
        *b |= 0xFE00;
    }

  if (linearization)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "linearization", &val);
      *linearization = (uint8_t)val;
    }

  if (analog_data_format)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP (obj_sdr_record, "sensor_unit1.analog_data_format", &val);
      *analog_data_format = (uint8_t) val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

static int
_sensor_decode_value (ipmi_sdr_parse_ctx_t ctx,
                      int8_t r_exponent,
                      int8_t b_exponent,
                      int16_t m, 
                      int16_t b, 
                      uint8_t linearization, 
                      uint8_t analog_data_format, 
                      uint8_t raw_data,
                      double **value_ptr)
{
  double reading;
  int rv = -1;
  
  assert(ctx);
  assert(ctx->magic == IPMI_SDR_PARSE_MAGIC);
  assert(value_ptr);
  
  if (ipmi_sensor_decode_value (r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &reading) < 0)
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INTERNAL_ERROR);
      goto cleanup;
    }
  
  if (!((*value_ptr) = (double *)malloc(sizeof(double))))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_OUT_OF_MEMORY);
      goto cleanup;
    }
  (**value_ptr) = reading;
  
  rv = 0;
 cleanup:
  return rv;
}

int
ipmi_sdr_parse_sensor_reading_ranges (ipmi_sdr_parse_ctx_t ctx,
                                      uint8_t *sdr_record,
                                      unsigned int sdr_record_len,
                                      double **nominal_reading,
                                      double **normal_maximum,
                                      double **normal_minimum,
                                      double **sensor_maximum_reading,
                                      double **sensor_minimum_reading)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  int8_t r_exponent, b_exponent;
  int16_t m, b;
  uint8_t linearization, analog_data_format;
  double *tmp_nominal_reading = NULL;
  double *tmp_normal_maximum = NULL;
  double *tmp_normal_minimum = NULL;
  double *tmp_sensor_maximum_reading = NULL;
  double *tmp_sensor_minimum_reading = NULL;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  if (nominal_reading)
    *nominal_reading = NULL;
  if (normal_maximum)
    *normal_maximum = NULL;
  if (normal_minimum)
    *normal_minimum = NULL;
  if (sensor_maximum_reading)
    *sensor_maximum_reading = NULL;
  if (sensor_minimum_reading)
    *sensor_minimum_reading = NULL;

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (ipmi_sdr_parse_sensor_decoding_data(ctx,
                                          sdr_record,
                                          sdr_record_len,
                                          &r_exponent,
                                          &b_exponent,
                                          &m,
                                          &b,
                                          &linearization,
                                          &analog_data_format) < 0)
    goto cleanup;

  if (!IPMI_SDR_ANALOG_DATA_FORMAT_VALID(analog_data_format))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_CANNOT_PARSE_OR_CALCULATE);
      goto cleanup;
    }

  if (!IPMI_SDR_LINEARIZATION_IS_LINEAR(linearization))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_CANNOT_PARSE_OR_CALCULATE);
      goto cleanup;
    }
 
  if (nominal_reading)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "nominal_reading",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_nominal_reading) < 0)
        goto cleanup;
    }
  if (normal_maximum)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "normal_maximum",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_normal_maximum) < 0)
        goto cleanup;
    }
  if (normal_minimum)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "normal_minimum",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_normal_minimum) < 0)
        goto cleanup;
    }
  if (sensor_maximum_reading)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "sensor_maximum_reading",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_sensor_maximum_reading) < 0)
        goto cleanup;
    }
  if (sensor_minimum_reading)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "sensor_minimum_reading",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_sensor_minimum_reading) < 0)
        goto cleanup;
    }

  if (nominal_reading)
    *nominal_reading = tmp_nominal_reading;
  if (normal_maximum)
    *normal_maximum = tmp_normal_maximum;
  if (normal_minimum)
    *normal_minimum = tmp_normal_minimum;
  if (sensor_maximum_reading)
    *sensor_maximum_reading = tmp_sensor_maximum_reading;
  if (sensor_minimum_reading)
    *sensor_minimum_reading = tmp_sensor_minimum_reading;

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  if (rv < 0)
    {
      if (tmp_nominal_reading)
        free(nominal_reading);
      if (tmp_normal_maximum)
        free(normal_maximum);
      if (tmp_normal_minimum)
        free(normal_minimum);
      if (tmp_sensor_maximum_reading)
        free(sensor_maximum_reading);
      if (tmp_sensor_minimum_reading)
        free(sensor_minimum_reading);
    }
  return rv;
}

int
ipmi_sdr_parse_thresholds (ipmi_sdr_parse_ctx_t ctx,
                           uint8_t *sdr_record,
                           unsigned int sdr_record_len,
                           double **lower_non_critical_threshold,
                           double **lower_critical_threshold,
                           double **lower_non_recoverable_threshold,
                           double **upper_non_critical_threshold,
                           double **upper_critical_threshold,
                           double **upper_non_recoverable_threshold)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  int8_t r_exponent, b_exponent;
  int16_t m, b;
  uint8_t linearization, analog_data_format;
  double *tmp_lower_non_critical_threshold = NULL;
  double *tmp_lower_critical_threshold = NULL;
  double *tmp_lower_non_recoverable_threshold = NULL;
  double *tmp_upper_non_critical_threshold = NULL;
  double *tmp_upper_critical_threshold = NULL;
  double *tmp_upper_non_recoverable_threshold = NULL;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  if (lower_non_critical_threshold)
    *lower_non_critical_threshold = NULL;
  if (lower_critical_threshold)
    *lower_critical_threshold = NULL;
  if (lower_non_recoverable_threshold)
    *lower_non_recoverable_threshold = NULL;
  if (upper_non_critical_threshold)
    *upper_non_critical_threshold = NULL;
  if (upper_critical_threshold)
    *upper_critical_threshold = NULL;
  if (upper_non_recoverable_threshold)
    *upper_non_recoverable_threshold = NULL;

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (ipmi_sdr_parse_sensor_decoding_data(ctx,
                                          sdr_record,
                                          sdr_record_len,
                                          &r_exponent,
                                          &b_exponent,
                                          &m,
                                          &b,
                                          &linearization,
                                          &analog_data_format) < 0)
    goto cleanup;

  if (!IPMI_SDR_ANALOG_DATA_FORMAT_VALID(analog_data_format))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_CANNOT_PARSE_OR_CALCULATE);
      goto cleanup;
    }

  if (!IPMI_SDR_LINEARIZATION_IS_LINEAR(linearization))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_CANNOT_PARSE_OR_CALCULATE);
      goto cleanup;
    }
 
  if (lower_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "lower_non_critical_threshold",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_lower_non_critical_threshold) < 0)
        goto cleanup;
    }
  if (lower_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "lower_critical_threshold",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_lower_critical_threshold) < 0)
        goto cleanup;
    }
  if (lower_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "lower_non_recoverable_threshold",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_lower_non_recoverable_threshold) < 0)
        goto cleanup;
    }
  if (upper_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "upper_non_critical_threshold",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_upper_non_critical_threshold) < 0)
        goto cleanup;
    }
  if (upper_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "upper_critical_threshold",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_upper_critical_threshold) < 0)
        goto cleanup;
    }
  if (upper_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "upper_non_recoverable_threshold",
                                     &val);

      if (_sensor_decode_value (ctx,
                                r_exponent,
                                b_exponent,
                                m,
                                b,
                                linearization,
                                analog_data_format,
                                val,
                                &tmp_upper_non_recoverable_threshold) < 0)
        goto cleanup;
    }

  if (lower_non_critical_threshold)
    *lower_non_critical_threshold = tmp_lower_non_critical_threshold;
  if (lower_critical_threshold)
    *lower_critical_threshold = tmp_lower_critical_threshold;
  if (lower_non_recoverable_threshold)
    *lower_non_recoverable_threshold = tmp_lower_non_recoverable_threshold;
  if (upper_non_critical_threshold)
    *upper_non_critical_threshold = tmp_upper_non_critical_threshold;
  if (upper_critical_threshold)
    *upper_critical_threshold = tmp_upper_critical_threshold;
  if (upper_non_recoverable_threshold)
    *upper_non_recoverable_threshold = tmp_upper_non_recoverable_threshold;

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  if (rv < 0)
    {
      if (tmp_lower_non_critical_threshold)
        free(lower_non_critical_threshold);
      if (tmp_lower_critical_threshold)
        free(lower_critical_threshold);
      if (tmp_lower_non_recoverable_threshold)
        free(lower_non_recoverable_threshold);
      if (tmp_upper_non_critical_threshold)
        free(upper_non_critical_threshold);
      if (tmp_upper_critical_threshold)
        free(upper_critical_threshold);
      if (tmp_upper_non_recoverable_threshold)
        free(upper_non_recoverable_threshold);
    }
  return rv;
}

int
ipmi_sdr_parse_thresholds_raw (ipmi_sdr_parse_ctx_t ctx,
                               uint8_t *sdr_record,
                               unsigned int sdr_record_len,
                               uint8_t *lower_non_critical_threshold,
                               uint8_t *lower_critical_threshold,
                               uint8_t *lower_non_recoverable_threshold,
                               uint8_t *upper_non_critical_threshold,
                               uint8_t *upper_critical_threshold,
                               uint8_t *upper_non_recoverable_threshold)
{
  fiid_obj_t obj_sdr_record = NULL;
  fiid_obj_t obj_sdr_record_threshold = NULL;
  uint32_t acceptable_record_types;
  uint8_t event_reading_type_code;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  /* We don't want the generic sdr full record, we need the special
   * threshold one.
   */

  if (ipmi_sdr_parse_event_reading_type_code (ctx,
                                              sdr_record,
                                              sdr_record_len,
                                              &event_reading_type_code) < 0)
    goto cleanup;

  if (!IPMI_EVENT_READING_TYPE_CODE_IS_THRESHOLD(event_reading_type_code))
    {
      SDR_PARSE_ERRNUM_SET(IPMI_SDR_PARSE_CTX_ERR_INVALID_SDR_RECORD)
        goto cleanup;
    }

  SDR_PARSE_FIID_OBJ_COPY_CLEANUP(obj_sdr_record_threshold,
                                  obj_sdr_record,
                                  tmpl_sdr_full_sensor_record_threshold_based_sensors);

  if (lower_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "lower_non_critical_threshold",
                                     &val);
      *lower_non_critical_threshold = val;
    }

  if (lower_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "lower_critical_threshold",
                                     &val);
      *lower_critical_threshold = val;
    }

  if (lower_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "lower_non_recoverable_threshold",
                                     &val);
      *lower_non_recoverable_threshold = val;
    }

  if (upper_non_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "upper_non_critical_threshold",
                                     &val);
      *upper_non_critical_threshold = val;
    }

  if (upper_critical_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "upper_critical_threshold",
                                     &val);
      *upper_critical_threshold = val;
    }

  if (upper_non_recoverable_threshold)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record_threshold,
                                     "upper_non_recoverable_threshold",
                                     &val);
      *upper_non_recoverable_threshold = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record_threshold);
  return rv;
}

int
ipmi_sdr_parse_hysteresis (ipmi_sdr_parse_ctx_t ctx,
                           uint8_t *sdr_record,
                           unsigned int sdr_record_len,
                           uint8_t *positive_going_threshold_hysteresis,
                           uint8_t *negative_going_threshold_hysteresis)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FULL_SENSOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_COMPACT_SENSOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (positive_going_threshold_hysteresis)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "positive_going_threshold_hysteresis",
                                     &val);
      *positive_going_threshold_hysteresis = val;
    }
  if (negative_going_threshold_hysteresis)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record,
                                     "negative_going_threshold_hysteresis",
                                     &val);
      *negative_going_threshold_hysteresis = val;
    }
  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_container_entity (ipmi_sdr_parse_ctx_t ctx,
                                 uint8_t *sdr_record,
                                 unsigned int sdr_record_len,
                                 uint8_t *container_entity_id,
                                 uint8_t *container_entity_instance)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_ENTITY_ASSOCIATION_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_DEVICE_RELATIVE_ENTITY_ASSOCIATION_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (container_entity_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "container_entity_id", &val);
      *container_entity_id = val;
    }

  if (container_entity_instance)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "container_entity_instance", &val);
      *container_entity_instance = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_device_id_string (ipmi_sdr_parse_ctx_t ctx,
                                 uint8_t *sdr_record,
                                 unsigned int sdr_record_len,
                                 char *device_id_string,
                                 unsigned int device_id_string_len)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_GENERIC_DEVICE_LOCATOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_FRU_DEVICE_LOCATOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (device_id_string && device_id_string_len)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP_DATA(obj_sdr_record,
                                          "device_id_string",
                                          (uint8_t *)device_id_string,
                                          device_id_string_len);
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_device_type (ipmi_sdr_parse_ctx_t ctx,
                            uint8_t *sdr_record,
                            unsigned int sdr_record_len,
                            uint8_t *device_type,
                            uint8_t *device_type_modifier)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_GENERIC_DEVICE_LOCATOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_FRU_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (device_type)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "device_type", &val);
      *device_type = val;
    }
  if (device_type_modifier)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "device_type_modifier", &val);
      *device_type_modifier = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_entity_id_and_instance (ipmi_sdr_parse_ctx_t ctx,
                                       uint8_t *sdr_record,
                                       unsigned int sdr_record_len,
                                       uint8_t *entity_id,
                                       uint8_t *entity_instance)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_GENERIC_DEVICE_LOCATOR_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (entity_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "entity_id", &val);
      *entity_id = val;
    }
  if (entity_instance)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "entity_instance", &val);
      *entity_instance = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_general_device_locator_parameters (ipmi_sdr_parse_ctx_t ctx,
                                                  uint8_t *sdr_record,
                                                  unsigned int sdr_record_len,
                                                  uint8_t *direct_access_address,
                                                  uint8_t *channel_number,
                                                  uint8_t *device_slave_address,
                                                  uint8_t *private_bus_id,
                                                  uint8_t *lun_for_master_write_read_command,
                                                  uint8_t *address_span)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val1, val2;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_GENERIC_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (direct_access_address)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "direct_access_address", &val);
      *direct_access_address = val;
    }
  if (channel_number)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "channel_number_ls", &val1);
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "channel_number_ms", &val2);
      *channel_number = (val1 << 3) | val2;
    }
  if (device_slave_address)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "device_slave_address", &val);
      *device_slave_address = val;
    }
  if (private_bus_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "private_bus_id", &val);
      *private_bus_id = val;
    }
  if (lun_for_master_write_read_command)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "lun_for_master_write_read_command", &val);
      *lun_for_master_write_read_command = val;
    }
  if (address_span)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "address_span", &val);
      *address_span = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_fru_device_locator_parameters (ipmi_sdr_parse_ctx_t ctx,
                                              uint8_t *sdr_record,
                                              unsigned int sdr_record_len,
                                              uint8_t *direct_access_address,
                                              uint8_t *logical_fru_device_device_slave_address,
                                              uint8_t *private_bus_id,
                                              uint8_t *lun_for_master_write_read_fru_command,
                                              uint8_t *logical_physical_fru_device,
                                              uint8_t *channel_number)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FRU_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (direct_access_address)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "direct_access_address", &val);
      *direct_access_address = val;
    }
  if (logical_fru_device_device_slave_address)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "logical_fru_device_device_slave_address", &val);
      *logical_fru_device_device_slave_address = val;
    }
  if (private_bus_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "private_bus_id", &val);
      *private_bus_id = val;
    }
  if (lun_for_master_write_read_fru_command)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "lun_for_master_write_read_fru_command", &val);
      *lun_for_master_write_read_fru_command = val;
    }
  if (logical_physical_fru_device)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "logical_physical_fru_device", &val);
      *logical_physical_fru_device = val;
    }
  if (channel_number)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "channel_number", &val);
      *channel_number = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_fru_entity_id_and_instance (ipmi_sdr_parse_ctx_t ctx,
                                           uint8_t *sdr_record,
                                           unsigned int sdr_record_len,
                                           uint8_t *fru_entity_id,
                                           uint8_t *fru_entity_instance)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_FRU_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (fru_entity_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "fru_entity_id", &val);
      *fru_entity_id = val;
    }
  if (fru_entity_instance)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "fru_entity_instance", &val);
      *fru_entity_instance = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_management_controller_device_locator_parameters (ipmi_sdr_parse_ctx_t ctx,
                                                                uint8_t *sdr_record,
                                                                unsigned int sdr_record_len,
                                                                uint8_t *device_slave_address,
                                                                uint8_t *channel_number)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_DEVICE_LOCATOR_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (device_slave_address)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "device_slave_address", &val);
      *device_slave_address = val;
    }
  if (channel_number)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "channel_number", &val);
      *channel_number = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_manufacturer_id (ipmi_sdr_parse_ctx_t ctx,
                                uint8_t *sdr_record,
                                unsigned int sdr_record_len,
                                uint32_t *manufacturer_id)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_CONFIRMATION_RECORD;
  acceptable_record_types |= IPMI_SDR_PARSE_RECORD_TYPE_OEM_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (manufacturer_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "manufacturer_id", &val);
      *manufacturer_id = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_product_id (ipmi_sdr_parse_ctx_t ctx,
                           uint8_t *sdr_record,
                           unsigned int sdr_record_len,
                           uint16_t *product_id)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  uint64_t val;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_MANAGEMENT_CONTROLLER_CONFIRMATION_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (product_id)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP(obj_sdr_record, "product_id", &val);
      *product_id = val;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

int
ipmi_sdr_parse_oem_data (ipmi_sdr_parse_ctx_t ctx,
                         uint8_t *sdr_record,
                         unsigned int sdr_record_len,
                         uint8_t *oem_data,
                         unsigned int *oem_data_len)
{
  fiid_obj_t obj_sdr_record = NULL;
  uint32_t acceptable_record_types;
  int32_t len;
  int rv = -1;

  ERR(ctx && ctx->magic == IPMI_SDR_PARSE_MAGIC);

  SDR_PARSE_ERR_PARAMETERS(sdr_record && sdr_record_len);

  acceptable_record_types = IPMI_SDR_PARSE_RECORD_TYPE_OEM_RECORD;

  if (!(obj_sdr_record = _sdr_record_get_common(ctx,
                                                sdr_record,
                                                sdr_record_len,
                                                acceptable_record_types)))
    goto cleanup;

  if (oem_data && oem_data_len && *oem_data_len)
    {
      SDR_PARSE_FIID_OBJ_GET_CLEANUP_DATA_LEN(len,
                                              obj_sdr_record,
                                              "oem_data",
                                              oem_data,
                                              *oem_data_len);
      *oem_data_len = len;
    }

  rv = 0;
  ctx->errnum = IPMI_SDR_PARSE_CTX_ERR_SUCCESS;
 cleanup:
  SDR_PARSE_FIID_OBJ_DESTROY(obj_sdr_record);
  return rv;
}

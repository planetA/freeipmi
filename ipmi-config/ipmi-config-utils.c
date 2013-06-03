/*
 * Copyright (C) 2003-2013 FreeIPMI Core Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#if STDC_HEADERS
#include <string.h>
#endif /* STDC_HEADERS */
#include <errno.h>
#include <assert.h>

#include "ipmi-config-utils.h"

#include "freeipmi-portability.h"
#include "pstdout.h"

int
ipmi_config_keypair_parse_string (const char *str,
                                  char **section_name,
                                  char **key_name,
                                  char **value)
{
  char *str_temp = NULL;
  char *section_name_tok = NULL;
  char *key_name_tok = NULL;
  char *value_tok = NULL;
  char *ptr;
  char *buf;
  int rv = -1;

  assert (str);
  assert (section_name);
  assert (key_name);
  assert (value);

  *section_name = NULL;
  *key_name = NULL;
  *value = NULL;

  if (!(str_temp = strdup (str)))
    {
      perror ("strdup");
      goto cleanup;
    }

  section_name_tok = strtok_r (str_temp, ":", &buf);
  key_name_tok = strtok_r (NULL, "=", &buf);
  value_tok = strtok_r (NULL, "\0", &buf);

  if (!(section_name_tok && key_name_tok))
    {
      fprintf (stderr,
               "Improperly input keypair '%s'\n",
               str);
      goto cleanup;
    }

  /* get rid of spaces stuck in the string */
  if (section_name_tok)
    section_name_tok = strtok_r (section_name_tok, " \t", &buf);
  if (key_name_tok)
    key_name_tok = strtok_r (key_name_tok, " \t", &buf);
  if (value_tok)
    value_tok = strtok_r (value_tok, " \t", &buf);

  if (section_name_tok)
    {
      if (!(ptr = strdup (section_name_tok)))
        {
          perror ("strdup");
          goto cleanup;
        }
      *section_name = ptr;
    }
  if (key_name_tok)
    {
      if (!(ptr = strdup (key_name_tok)))
        {
          perror ("strdup");
          goto cleanup;
        }
      *key_name = ptr;
    }
  if (value_tok)
    {
      if (!(ptr = strdup (value_tok)))
        {
          perror ("strdup");
          goto cleanup;
        }
      *value = ptr;
    }
  else
    {
      /* values can be empty strings */
      if (!(ptr = strdup ("")))
        {
          perror ("strdup");
          goto cleanup;
        }
      *value = ptr;
    }

  rv = 0;
 cleanup:
  free (str_temp);
  if (rv < 0)
    {
      free (*section_name);
      *section_name = NULL;
      free (*key_name);
      *key_name = NULL;
      free (*value);
      *value = NULL;
    }
  return (rv);
}

int
ipmi_config_keypair_append (struct ipmi_config_keypair **keypairs,
                            struct ipmi_config_keypair *keypair)
{
  assert (keypairs);
  assert (keypair);

  if (*keypairs)
    {
      struct ipmi_config_keypair *kp;

      kp = *keypairs;
      while (kp)
        {
          if (!strcasecmp (kp->section_name, keypair->section_name)
              && !strcasecmp (kp->key_name, keypair->key_name))
            {
              fprintf (stderr,
                       "Duplicate section:key pair '%s:%s' specified\n",
                       kp->section_name, kp->key_name);
              return (-1);
            }
          kp = kp->next;
        }

      kp = *keypairs;
      while (kp->next)
        kp = kp->next;
      kp->next = keypair;
    }
  else
    *keypairs = keypair;

  return (0);
}

void
ipmi_config_keypairs_destroy (struct ipmi_config_keypair *keypairs)
{
  while (keypairs)
    {
      struct ipmi_config_keypair *kp_next = keypairs->next;
      ipmi_config_keypair_destroy (keypairs);
      keypairs = kp_next;
    }
}

struct ipmi_config_keypair *
ipmi_config_keypair_create (const char *section_name,
                            const char *key_name,
                            const char *value_input)
{
  struct ipmi_config_keypair *keypair = NULL;

  assert (section_name);
  assert (key_name);

  if (!(keypair = (struct ipmi_config_keypair *)malloc (sizeof (struct ipmi_config_keypair))))
    {
      perror ("malloc");
      goto cleanup;
    }
  memset (keypair, '\0', sizeof (struct ipmi_config_keypair));

  if (!(keypair->section_name = strdup (section_name)))
    {
      perror ("strdup");
      goto cleanup;
    }

  if (!(keypair->key_name = strdup (key_name)))
    {
      perror ("strdup");
      goto cleanup;
    }

  if (value_input)
    {
      if (!(keypair->value_input = strdup (value_input)))
        {
          perror ("strdup");
          goto cleanup;
        }
    }

  return (keypair);

 cleanup:
  ipmi_config_keypair_destroy (keypair);
  return (NULL);
}

void
ipmi_config_keypair_destroy (struct ipmi_config_keypair *keypair)
{
  if (keypair)
    {
      free (keypair->section_name);
      free (keypair->key_name);
      free (keypair->value_input);
      free (keypair);
    }
}

struct ipmi_config_section_str *
ipmi_config_section_str_create (const char *section_name)
{
  struct ipmi_config_section_str *sstr = NULL;

  if (!(sstr = (struct ipmi_config_section_str *)malloc (sizeof (struct ipmi_config_section_str))))
    {
      perror ("malloc");
      goto cleanup;
    }

  if (!(sstr->section_name = strdup (section_name)))
    {
      perror ("strdup");
      goto cleanup;
    }
  sstr->next = NULL;

  return (sstr);

 cleanup:
  ipmi_config_section_str_destroy (sstr);
  return (NULL);
}

int
ipmi_config_section_str_append (struct ipmi_config_section_str **section_strs,
                                struct ipmi_config_section_str *section_str)
{
  assert (section_strs);
  assert (section_str);

  if (*section_strs)
    {
      struct ipmi_config_section_str *sstr;

      sstr = *section_strs;
      while (sstr)
        {
          if (!strcasecmp (sstr->section_name, section_str->section_name))
            {
              fprintf (stderr,
                       "Duplicate section '%s' specified\n",
                       sstr->section_name);
              return (-1);
            }
          sstr = sstr->next;
        }

      sstr = *section_strs;
      while (sstr->next)
        sstr = sstr->next;
      sstr->next = section_str;
    }
  else
    *section_strs = section_str;

  return (0);
}

void
ipmi_config_section_str_destroy (struct ipmi_config_section_str *section_str)
{
  if (section_str)
    {
      free (section_str->section_name);
      free (section_str);
    }
}

int
ipmi_config_ipv4_address_string2int (ipmi_config_state_data_t *state_data,
                                     const char *src,
                                     uint32_t *dest)
{
  unsigned int b1, b2, b3, b4;
  uint64_t val;
  int ret;

  assert (state_data);
  assert (src);
  assert (dest);

  if ((ret = sscanf (src,
                     "%u.%u.%u.%u",
                     &b1,
                     &b2,
                     &b3,
                     &b4)) < 0)
    {
      pstdout_perror (state_data->pstate, "sscanf");
      return (-1);
    }

  if (ret != 4)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_config_ipv4_address_string2int: Invalid src input: %s\n",
                       src);
      return (-1);
    }

  val = 0;
  val |= (uint64_t)b1;
  val |= ((uint64_t)b2 << 8);
  val |= ((uint64_t)b3 << 16);
  val |= ((uint64_t)b4 << 24);

  *dest = val;
  return (0);
}

int
ipmi_config_mac_address_string2int (ipmi_config_state_data_t *state_data,
                                    const char *src,
                                    uint64_t *dest)
{
  unsigned int b1, b2, b3, b4, b5, b6;
  uint64_t val;
  int ret;

  assert (state_data);
  assert (src);
  assert (dest);

  if ((ret = sscanf (src,
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     &b1,
                     &b2,
                     &b3,
                     &b4,
                     &b5,
                     &b6)) < 0)
    {
      pstdout_perror (state_data->pstate, "sscanf");
      return (-1);
    }

  if (ret != 6)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_config_mac_address_string2int: Invalid src input: %s\n",
                       src);
      return (-1);
    }

  val = 0;
  val |= (uint64_t)b1;
  val |= ((uint64_t)b2 << 8);
  val |= ((uint64_t)b3 << 16);
  val |= ((uint64_t)b4 << 24);
  val |= ((uint64_t)b5 << 32);
  val |= ((uint64_t)b6 << 40);

  *dest = val;
  return (0);
}

struct ipmi_config_section *
ipmi_config_find_section (ipmi_config_state_data_t *state_data,
                          const char *section_name)
{
  struct ipmi_config_section *s = NULL;

  assert (state_data);
  assert (section_name);

  s = state_data->sections;
  while (s)
    {
      if (!strcasecmp (section_name, s->section_name))
        break;
      s = s->next;
    }

  return (s);
}

struct ipmi_config_key *
ipmi_config_find_key (struct ipmi_config_section *section,
                      const char *key_name)
{
  struct ipmi_config_key *k = NULL;

  assert (section);
  assert (key_name);

  k = section->keys;
  while (k)
    {
      if (!strcasecmp (key_name, k->key_name))
        break;
      k = k->next;
    }

  return (k);
}

struct ipmi_config_keyvalue *
ipmi_config_find_keyvalue (struct ipmi_config_section *section,
                           const char *key_name)
{
  struct ipmi_config_keyvalue *kv = NULL;

  assert (section);
  assert (key_name);

  kv = section->keyvalues;
  while (kv)
    {
      if (!strcasecmp (key_name, kv->key->key_name))
        break;
      kv = kv->next;
    }

  return (kv);
}

int
ipmi_config_is_non_fatal_error (ipmi_ctx_t ipmi_ctx,
                                fiid_obj_t obj_cmd_rs,
                                ipmi_config_err_t *non_fatal_err)
{
  assert (ipmi_ctx);
  assert (obj_cmd_rs);
  assert (fiid_obj_valid (obj_cmd_rs));
  assert (non_fatal_err);

  if (!IPMI_CTX_ERRNUM_IS_FATAL_ERROR (ipmi_ctx))
    {
      if (ipmi_ctx_errnum (ipmi_ctx) == IPMI_ERR_BAD_COMPLETION_CODE
          && (ipmi_check_completion_code (obj_cmd_rs,
                                          IPMI_COMP_CODE_INVALID_DATA_FIELD_IN_REQUEST) == 1
              || ipmi_check_completion_code (obj_cmd_rs,
                                             IPMI_COMP_CODE_REQUEST_PARAMETER_NOT_SUPPORTED) == 1))
        (*non_fatal_err) = IPMI_CONFIG_ERR_NON_FATAL_ERROR_INVALID_UNSUPPORTED_CONFIG;
      else
        (*non_fatal_err) = IPMI_CONFIG_ERR_NON_FATAL_ERROR;
      return (1);
    }
  
  return (0);
}

int
ipmi_config_is_config_param_non_fatal_error (ipmi_ctx_t ipmi_ctx,
                                             fiid_obj_t obj_cmd_rs,
                                             ipmi_config_err_t *non_fatal_err)
{
  assert (ipmi_ctx);
  assert (obj_cmd_rs);
  assert (fiid_obj_valid (obj_cmd_rs));
  assert (non_fatal_err);

  /* should all be the same, this is just to check */

  assert (IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED == IPMI_COMP_CODE_SET_PEF_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED);
  assert (IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED == IPMI_COMP_CODE_SET_SOL_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED);
  assert (IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED == IPMI_COMP_CODE_SET_BOOT_OPTIONS_PARAMETER_NOT_SUPPORTED);

  assert (IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_WRITE_READ_ONLY_PARAMETER == IPMI_COMP_CODE_SET_PEF_CONFIGURATION_PARAMETERS_WRITE_READ_ONLY_PARAMETER);
  assert (IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_WRITE_READ_ONLY_PARAMETER == IPMI_COMP_CODE_SET_SOL_CONFIGURATION_PARAMETERS_WRITE_READ_ONLY_PARAMETER);
  assert (IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_WRITE_READ_ONLY_PARAMETER == IPMI_COMP_CODE_SET_BOOT_OPTIONS_WRITE_READ_ONLY_PARAMETER);
  
  if (!IPMI_CTX_ERRNUM_IS_FATAL_ERROR (ipmi_ctx))
    {
      if (ipmi_ctx_errnum (ipmi_ctx) == IPMI_ERR_BAD_COMPLETION_CODE
          && ipmi_check_completion_code (obj_cmd_rs,
                                         IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_WRITE_READ_ONLY_PARAMETER) == 1)
        (*non_fatal_err) = IPMI_CONFIG_ERR_NON_FATAL_ERROR_READ_ONLY;
      else if (ipmi_ctx_errnum (ipmi_ctx) == IPMI_ERR_BAD_COMPLETION_CODE
               && ipmi_check_completion_code (obj_cmd_rs,
                                              IPMI_COMP_CODE_SET_LAN_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED) == 1)
        (*non_fatal_err) = IPMI_CONFIG_ERR_NON_FATAL_ERROR_NOT_SUPPORTED;
      else if (ipmi_ctx_errnum (ipmi_ctx) == IPMI_ERR_BAD_COMPLETION_CODE
               && (ipmi_check_completion_code (obj_cmd_rs,
                                               IPMI_COMP_CODE_INVALID_DATA_FIELD_IN_REQUEST) == 1
                   || ipmi_check_completion_code (obj_cmd_rs,
                                                  IPMI_COMP_CODE_REQUEST_PARAMETER_NOT_SUPPORTED) == 1))
        (*non_fatal_err) = IPMI_CONFIG_ERR_NON_FATAL_ERROR_INVALID_UNSUPPORTED_CONFIG;
      else
        (*non_fatal_err) = IPMI_CONFIG_ERR_NON_FATAL_ERROR;
      return (1);
    }

  return (0);
}

int
ipmi_config_pstdout_fprintf (ipmi_config_state_data_t *state_data,
                             FILE *stream,
                             const char *format, ...)
{
  va_list ap;
  int rv; 

  assert (state_data);

  /* special case b/c pstdout doesn't handle non-stdout/non-stderr
   * assume proper checks in tools if stream != stdout || != stderr
   */

  va_start (ap, format);

  if (stream == stdout || stream == stderr)
    rv = pstdout_vfprintf (state_data->pstate, stream, format, ap);
  else
    rv = vfprintf (stream, format, ap);

  va_end (ap);

  return rv;
}

ipmi_config_err_t
load_lan_channel_numbers (ipmi_config_state_data_t *state_data)
{
  int ret;

  assert (state_data);

  if ((ret = ipmi_get_channel_numbers (state_data->ipmi_ctx,
                                       IPMI_CHANNEL_MEDIUM_TYPE_LAN_802_3,
                                       state_data->lan_channel_numbers,
                                       IPMI_CHANNEL_NUMBERS_MAX)) < 0)
    {
      if (state_data->prog_data->args->common_args.debug)
        pstdout_fprintf (state_data->pstate,
                         stderr,
                         "ipmi_get_channel_numbers: %s\n",
                         ipmi_ctx_errormsg (state_data->ipmi_ctx));
      return (IPMI_CONFIG_ERR_NON_FATAL_ERROR);
    }
  
  state_data->lan_channel_numbers_count = (unsigned int)ret;
  state_data->lan_channel_numbers_loaded++;

  return (IPMI_CONFIG_ERR_SUCCESS);
}

ipmi_config_err_t
load_serial_channel_numbers (ipmi_config_state_data_t *state_data)
{
  int ret;

  assert (state_data);

  if ((ret = ipmi_get_channel_numbers (state_data->ipmi_ctx,
                                       IPMI_CHANNEL_MEDIUM_TYPE_RS232,
                                       state_data->serial_channel_numbers,
                                       IPMI_CHANNEL_NUMBERS_MAX)) < 0)
    {
      if (state_data->prog_data->args->common_args.debug)
        pstdout_fprintf (state_data->pstate,
                         stderr,
                         "ipmi_get_channel_numbers: %s\n",
                         ipmi_ctx_errormsg (state_data->ipmi_ctx));
      return (IPMI_CONFIG_ERR_NON_FATAL_ERROR);
    }
  
  state_data->serial_channel_numbers_count = (unsigned int)ret;
  state_data->serial_channel_numbers_loaded++;

  return (IPMI_CONFIG_ERR_SUCCESS);
}

static void
_sol_channel_number_save (ipmi_config_state_data_t *state_data,
                          uint8_t lan_channel_number,
                          uint8_t sol_channel_number)
{
  unsigned int i;
  int found = 0;

  assert (state_data);
  assert (state_data->sol_channel_numbers_count <= IPMI_CHANNEL_NUMBERS_MAX);

  for (i = 0; i < state_data->sol_channel_numbers_count; i++)
    {
      if (state_data->sol_channel_numbers_sol_channel[i] == sol_channel_number)
        {
          found++;
          break;
        }
    }

  if (!found)
    {
      state_data->sol_channel_numbers_unique[state_data->sol_channel_numbers_unique_count] = sol_channel_number;
      state_data->sol_channel_numbers_unique_count++;
    }

  state_data->sol_channel_numbers_lan_channel[state_data->sol_channel_numbers_count] = lan_channel_number;
  state_data->sol_channel_numbers_sol_channel[state_data->sol_channel_numbers_count] = sol_channel_number;
  state_data->sol_channel_numbers_count++;

}

ipmi_config_err_t
_get_sol_channel_number_for_channel (ipmi_config_state_data_t *state_data,
                                     uint8_t lan_channel_number)
{
  ipmi_config_err_t rv = IPMI_CONFIG_ERR_FATAL_ERROR;
  ipmi_config_err_t ret;
  fiid_obj_t obj_cmd_rs = NULL;
  uint64_t val;

  assert (state_data);

  if (state_data->prog_data->args->common_args.section_specific_workaround_flags & IPMI_PARSE_SECTION_SPECIFIC_WORKAROUND_FLAGS_SOL_CHANNEL_ASSUME_LAN_CHANNEL)
    {
      _sol_channel_number_save (state_data, lan_channel_number, lan_channel_number);
      goto out;
    }

  if (!(obj_cmd_rs = fiid_obj_create (tmpl_cmd_get_sol_configuration_parameters_sol_payload_channel_rs)))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_create: %s\n",
                       strerror (errno));
      goto cleanup;
    }

  if (ipmi_cmd_get_sol_configuration_parameters_sol_payload_channel (state_data->ipmi_ctx,
                                                                     lan_channel_number,
                                                                     IPMI_GET_SOL_PARAMETER,
                                                                     IPMI_SOL_CONFIGURATION_PARAMETERS_NO_SET_SELECTOR,
                                                                     IPMI_SOL_CONFIGURATION_PARAMETERS_NO_BLOCK_SELECTOR,
                                                                     obj_cmd_rs) < 0)
    {
      /* This parameter is optional, if its not supported, assume LAN channel */
      if (ipmi_ctx_errnum (state_data->ipmi_ctx) == IPMI_ERR_BAD_COMPLETION_CODE
          && (ipmi_check_completion_code (obj_cmd_rs,
                                          IPMI_COMP_CODE_SET_SOL_CONFIGURATION_PARAMETERS_PARAMETER_NOT_SUPPORTED) == 1))
        {
          _sol_channel_number_save (state_data, lan_channel_number, lan_channel_number);
          goto out;
        }

      if (state_data->prog_data->args->common_args.debug)
        pstdout_fprintf (state_data->pstate,
                         stderr,
                         "ipmi_cmd_get_sol_configuration_parameters_sol_payload_channel: %s\n",
                         ipmi_ctx_errormsg (state_data->ipmi_ctx));
      
      if (ipmi_config_is_config_param_non_fatal_error (state_data->ipmi_ctx,
                                                       obj_cmd_rs,
                                                       &ret))
        rv = ret;
      
      goto cleanup;
    }

  if (FIID_OBJ_GET (obj_cmd_rs,
                    "payload_channel",
                    &val) < 0)
    {
      rv = IPMI_CONFIG_ERR_NON_FATAL_ERROR;
      goto cleanup;
    }

  _sol_channel_number_save (state_data, lan_channel_number, val);

 out:
  rv = IPMI_CONFIG_ERR_SUCCESS;
 cleanup:
  fiid_obj_destroy (obj_cmd_rs);
  return (rv);
}

ipmi_config_err_t
load_sol_channel_numbers (ipmi_config_state_data_t *state_data)
{
  unsigned int channelindex;

  assert (state_data);
  
  /* There's a lot of trickery here.  Even if motherboard specifies
   * multiple LAN channels, they could map to a fewer number of SOL
   * channels.  So we need to calculate the number of unique SOL
   * channels
   */

  if (state_data->lan_channel_numbers_count > 0)
    {
      for (channelindex = 0; channelindex < state_data->lan_channel_numbers_count; channelindex++)
        {
          if (_get_sol_channel_number_for_channel (state_data,
                                                   state_data->lan_channel_numbers[channelindex]) == IPMI_CONFIG_ERR_FATAL_ERROR)
            return (IPMI_CONFIG_ERR_FATAL_ERROR);
        }
      state_data->sol_channel_numbers_loaded++;
    }

  return (IPMI_CONFIG_ERR_SUCCESS);
}

ipmi_config_err_t
get_lan_channel_number (ipmi_config_state_data_t *state_data,
                        const char *section_name,
                        uint8_t *channel_number)
{
  ipmi_config_err_t rv = IPMI_CONFIG_ERR_FATAL_ERROR;
  ipmi_config_err_t ret;

  assert (state_data);
  /* section_name can be NULL if want to force IPMI search */
  assert (channel_number);

  /* For multi-channel cases, channel will be in the section name */

  if (section_name)
    {
      char *ptr;

      /* Special case for Lan_Channel_Channel_X */
      if ((ptr = stristr (section_name, "Channel_Channel_")))
        {
          (*channel_number) = atoi (ptr + strlen ("Channel_Channel_"));
          return (IPMI_CONFIG_ERR_SUCCESS);
        }

      /* For all other sections with a channel number at the end of the section name */
      if ((ptr = stristr (section_name, "Channel_")))
        {
          (*channel_number) = atoi (ptr + strlen ("Channel_"));
          return (IPMI_CONFIG_ERR_SUCCESS);
        }
    }

  /* for single-channel, channel is first one found */

  if (!state_data->prog_data->args->lan_channel_number_set)
    {
      if (!state_data->lan_channel_numbers_loaded)
        {
          if ((ret = load_lan_channel_numbers (state_data)) != IPMI_CONFIG_ERR_SUCCESS)
            {
              rv = ret;
              goto cleanup;
            }
        }

      if (!state_data->lan_channel_numbers_count)
        {
          rv = IPMI_CONFIG_ERR_NON_FATAL_ERROR;
          goto cleanup;
        }

      (*channel_number) = state_data->lan_channel_numbers[0];
    }
  else
    (*channel_number) = state_data->prog_data->args->lan_channel_number;
  
  rv = IPMI_CONFIG_ERR_SUCCESS;
 cleanup:
  return (rv);
}

ipmi_config_err_t
get_serial_channel_number (ipmi_config_state_data_t *state_data,
                           const char *section_name,
                           uint8_t *channel_number)
{
  ipmi_config_err_t rv = IPMI_CONFIG_ERR_FATAL_ERROR;
  ipmi_config_err_t ret;
 
  assert (state_data);
  /* section_name can be NULL if want to force IPMI search */
  assert (channel_number);

  /* For multi-channel cases, channel will be in the section name */

  if (section_name)
    {
      char *ptr;

      /* Special case for Serial_Channel_Channel_X */
      if ((ptr = stristr (section_name, "Channel_Channel_")))
        {
          (*channel_number) = atoi (ptr + strlen ("Channel_Channel_"));
          return (IPMI_CONFIG_ERR_SUCCESS);
        }

      /* For all other sections with a channel number at the end of the section name */
      if ((ptr = stristr (section_name, "Channel_")))
        {
          (*channel_number) = atoi (ptr + strlen ("Channel_"));
          return (IPMI_CONFIG_ERR_SUCCESS);
        }
    }

  /* for single-channel, channel is first one found */

  if (!state_data->prog_data->args->serial_channel_number_set)
    {
      if (!state_data->serial_channel_numbers_loaded)
        {
          if ((ret = load_serial_channel_numbers (state_data)) != IPMI_CONFIG_ERR_SUCCESS)
            {
              rv = ret;
              goto cleanup;
            }
        }

      if (!state_data->serial_channel_numbers_count)
        {
          rv = IPMI_CONFIG_ERR_NON_FATAL_ERROR;
          goto cleanup;
        }

      (*channel_number) = state_data->serial_channel_numbers[0];
    }
  else
    (*channel_number) = state_data->prog_data->args->serial_channel_number;
  
  rv = IPMI_CONFIG_ERR_SUCCESS;
 cleanup:
  return (rv);
}

ipmi_config_err_t
get_sol_channel_number (ipmi_config_state_data_t *state_data,
                        const char *section_name,
                        uint8_t *channel_number)
{
  ipmi_config_err_t rv = IPMI_CONFIG_ERR_FATAL_ERROR;
  ipmi_config_err_t ret;

  assert (state_data);
  /* section_name can be NULL if want to force IPMI search */
  assert (channel_number);

  /* For multi-channel cases, channel will be in the section name */

  if (section_name)
    {
      char *ptr;

      /* Sections with a channel number at the end of the section name */
      if ((ptr = stristr (section_name, "Channel_")))
        {
          (*channel_number) = atoi (ptr + strlen ("Channel_"));
          return (IPMI_CONFIG_ERR_SUCCESS);
        }
    }

  /* for single-channel, channel is first one found */

  if (!state_data->prog_data->args->sol_channel_number_set)
    {
      if (!state_data->sol_channel_numbers_loaded)
        {
          if ((ret = load_sol_channel_numbers (state_data)) != IPMI_CONFIG_ERR_SUCCESS)
            {
              rv = ret;
              goto cleanup;
            }
        }
      
      if (!state_data->sol_channel_numbers_count)
        {
          rv = IPMI_CONFIG_ERR_NON_FATAL_ERROR;
          goto cleanup;
        }
      
      (*channel_number) = state_data->sol_channel_numbers_unique[0];
    }
  else
    (*channel_number) = state_data->prog_data->args->sol_channel_number;
  
  rv = IPMI_CONFIG_ERR_SUCCESS;
 cleanup:
  return (rv);
}


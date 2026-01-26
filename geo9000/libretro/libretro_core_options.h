#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

/*
 ********************************
 * VERSION: 1.3
 ********************************
 *
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_definition option_defs_us[] = {
   {
      "geolith_system_type",
      "System Type (Restart)",
      "Specify the System Type: AES, MVS, or Universe BIOS System",
      {
         { "aes", "Neo Geo AES (Home Console)" },
         { "mvs", "Neo Geo MVS (Arcade)" },
         { "uni", "Universe BIOS (Community-enhanced BIOS)" },
         { NULL, NULL },
      },
      "aes"
   },
   {
      "geolith_unibios_hw",
      "Universe BIOS Hardware (Restart)",
      "Specify the hardware the Universe BIOS should detect",
      {
         { "aes", "Neo Geo AES (Home Console)" },
         { "mvs", "Neo Geo MVS (Arcade)" },
         { NULL, NULL },
      },
      "mvs"
   },
   {
      "geolith_region",
      "Region (Restart)",
      "Specify the Region: USA, Japan, Asia, Europe",
      {
         { "us", "USA" },
         { "jp", "Japan" },
         { "as", "Asia" },
         { "eu", "Europe" },
         { NULL, NULL },
      },
      "us"
   },
   {
      "geolith_memcard",
      "Memory Card",
      "Enable or Disable the Memory Card",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "on"
   },
   {
      "geolith_memcard_wp",
      "Memory Card Write Protect",
      "Enable or Disable the Memory Card Write Protect pin",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "geolith_settingmode",
      "Setting Mode (Restart, DIP Switch)",
      "Bring up the System ROM menu at boot on arcade systems",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "geolith_force_int_timing",
      "Force Integer Timing (Restart)",
      "Force the use of exactly 60Hz internal refresh rate and 48000Hz audio "
      "sample rate",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "geolith_4player",
      "Four Player Mode (Restart, Asia/Japan MVS Only)",
      "Set Four Player (dual MVS cabinet) mode for Asia/Japan MVS systems",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "geolith_freeplay",
      "Freeplay (DIP Switch)",
      "Play MVS games without the need to insert coins",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "geolith_overscan_t",
      "Mask Overscan (Top)",
      "Mask off pixels hidden by a bezel or border on original CRTs (top)",
      {
         { "16", NULL },
         { "12", NULL },
         { "8", NULL },
         { "4", NULL },
         { "0", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "geolith_overscan_b",
      "Mask Overscan (Bottom)",
      "Mask off pixels hidden by a bezel or border on original CRTs (bottom)",
      {
         { "16", NULL },
         { "12", NULL },
         { "8", NULL },
         { "4", NULL },
         { "0", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "geolith_overscan_l",
      "Mask Overscan (Left)",
      "Mask off pixels hidden by a bezel or border on original CRTs (left)",
      {
         { "16", NULL },
         { "12", NULL },
         { "8", NULL },
         { "4", NULL },
         { "0", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "geolith_overscan_r",
      "Mask Overscan (Right)",
      "Mask off pixels hidden by a bezel or border on original CRTs (right)",
      {
         { "16", NULL },
         { "12", NULL },
         { "8", NULL },
         { "4", NULL },
         { "0", NULL },
         { NULL, NULL },
      },
      "8"
   },
   {
      "geolith_palette",
      "Palette",
      "Set the Palette",
      {
         { "resnet", "Resistor Network" },
         { "raw", "Raw" },
         { NULL, NULL },
      },
      "resnet"
   },
   {
      "geolith_aspect",
      "Aspect Ratio",
      "Set the Aspect Ratio",
      {
         { "1:1", "Perfectly Square Pixels (1:1 PAR)" },
         { "45:44", "Ostensibly Accurate NTSC Aspect Ratio (45:44 PAR)" },
         { "4:3", "Very Traditional NTSC Aspect Ratio (4:3 DAR)" },
         { NULL, NULL },
      },
      "1:1"
   },
   {
      "geolith_sprlimit",
      "Sprites-per-line limit (Hack)",
      "Set the sprites-per-line limit - increasing causes glitches in some games",
      {
         { "96", "Hardware Accurate (96)" },
         { "192", "Double (192)" },
         { "288", "Triple (288)" },
         { "381", "MAX 381 MEGA PRO-GEAR SPEC" },
         { NULL, NULL },
      },
      "96"
   },
   {
      "geolith_oc",
      "Overclocking (Hack)",
      "Annihilate your accuracy with The 24MHz Shock - expect glitches",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   {
      "geolith_disable_adpcm_wrap",
      "Disable ADPCM Accumulator Wrap (Hack)",
      "ADPCM Accumulator Wrap may be disabled to fix sound effects in buggy "
      "games, for example Ganryu and Nightmare in the Dark. This is a hack, "
      "and should remain Off for most games.",
      {
         { "off", "Off" },
         { "on", "On" },
         { NULL, NULL },
      },
      "off"
   },
   { NULL, NULL, NULL, {{0}}, NULL },
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_option_definition *option_defs_intl[RETRO_LANGUAGE_LAST] = {
   option_defs_us, /* RETRO_LANGUAGE_ENGLISH */
   NULL,           /* RETRO_LANGUAGE_JAPANESE */
   NULL,           /* RETRO_LANGUAGE_FRENCH */
   NULL,           /* RETRO_LANGUAGE_SPANISH */
   NULL,           /* RETRO_LANGUAGE_GERMAN */
   NULL,           /* RETRO_LANGUAGE_ITALIAN */
   NULL,           /* RETRO_LANGUAGE_DUTCH */
   NULL,           /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,           /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,           /* RETRO_LANGUAGE_RUSSIAN */
   NULL,           /* RETRO_LANGUAGE_KOREAN */
   NULL,           /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,           /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,           /* RETRO_LANGUAGE_ESPERANTO */
   NULL,           /* RETRO_LANGUAGE_POLISH */
   NULL,           /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,           /* RETRO_LANGUAGE_ARABIC */
   NULL,           /* RETRO_LANGUAGE_GREEK */
   NULL,           /* RETRO_LANGUAGE_TURKISH */
   NULL,           /* RETRO_LANGUAGE_SLOVAK */
   NULL,           /* RETRO_LANGUAGE_PERSIAN */
   NULL,           /* RETRO_LANGUAGE_HEBREW */
   NULL,           /* RETRO_LANGUAGE_ASTURIAN */
   NULL,           /* RETRO_LANGUAGE_FINNISH */

};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb)
{
   unsigned version = 0;

   if (!environ_cb)
      return;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version >= 1))
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_intl core_options_intl;
      unsigned language = 0;

      core_options_intl.us    = option_defs_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = option_defs_intl[language];

      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_intl);
#else
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, &option_defs_us);
#endif
   }
   else
   {
      size_t i;
      size_t num_options               = 0;
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine number of options */
      for (;;)
      {
         if (!option_defs_us[num_options].key)
            break;
         num_options++;
      }

      /* Allocate arrays */
      variables  = (struct retro_variable *)calloc(num_options + 1, sizeof(struct retro_variable));
      values_buf = (char **)calloc(num_options, sizeof(char *));

      if (!variables || !values_buf)
         goto error;

      /* Copy parameters from option_defs_us array */
      for (i = 0; i < num_options; i++)
      {
         const char *key                        = option_defs_us[i].key;
         const char *desc                       = option_defs_us[i].desc;
         const char *default_value              = option_defs_us[i].default_value;
         struct retro_core_option_value *values = option_defs_us[i].values;
         size_t buf_len                         = 3;
         size_t default_index                   = 0;

         values_buf[i] = NULL;

         if (desc)
         {
            size_t num_values = 0;

            /* Determine number of values */
            for (;;)
            {
               if (!values[num_values].value)
                  break;

               /* Check if this is the default value */
               if (default_value)
                  if (strcmp(values[num_values].value, default_value) == 0)
                     default_index = num_values;

               buf_len += strlen(values[num_values].value);
               num_values++;
            }

            /* Build values string */
            if (num_values > 0)
            {
               size_t j;

               buf_len += num_values - 1;
               buf_len += strlen(desc);

               values_buf[i] = (char *)calloc(buf_len, sizeof(char));
               if (!values_buf[i])
                  goto error;

               strcpy(values_buf[i], desc);
               strcat(values_buf[i], "; ");

               /* Default value goes first */
               strcat(values_buf[i], values[default_index].value);

               /* Add remaining values */
               for (j = 0; j < num_values; j++)
               {
                  if (j != default_index)
                  {
                     strcat(values_buf[i], "|");
                     strcat(values_buf[i], values[j].value);
                  }
               }
            }
         }

         variables[i].key   = key;
         variables[i].value = values_buf[i];
      }

      /* Set variables */
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

error:

      /* Clean up */
      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif

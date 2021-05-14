/* CRT SwitchRes Core
 * Copyright (C) 2018 Alphanu / Ben Templeman.
 *
 * RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libretro.h>
#include <math.h>

#include "../retroarch.h"
#include <retro_common_api.h>
#include "video_crt_switch.h"
#include "video_display_server.h"
#include "../core_info.h"
#include "../verbosity.h"
#include "gfx_display.h"


#ifdef __linux__
#define LIBSWR "libswitchres.so"
#elif _WIN32
#define LIBSWR "libswitchres.dll"
#endif

#include "switchres_wrapper.h"


static LIBTYPE dlp;
static srAPI* SRobj;
static sr_mode srm;

static bool sr2_active = false;
static int super_width                    = 0;
int rtn       =0;
static bool menu_active = false;
static unsigned int fb_width = 0;
static unsigned int fb_height = 0;

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#if defined(HAVE_VIDEOCORE)
#include "include/userland/interface/vmcs_host/vc_vchi_gencmd.h"
static void crt_rpi_switch(int width, int height, float hz, int xoffset);
#endif

static void switch_crt_hz(videocrt_switch_t *p_switch)
{
   video_monitor_set_refresh_rate(p_switch->ra_core_hz);
   p_switch->ra_tmp_core_hz = p_switch->ra_core_hz;
}


static void crt_aspect_ratio_switch(
      videocrt_switch_t *p_switch,
      unsigned width, unsigned height)
{
  
   /* send aspect float to video_driver */
   RARCH_LOG("[CRT]: Setting Video Screen Size to: %dx%d \n", width, height);
   video_driver_set_size(width , height); 
   video_driver_set_viewport(width , height,1,1);

   p_switch->fly_aspect = (float)width / (float)height;
   video_driver_set_aspect_ratio_value((float)p_switch->fly_aspect);
   RARCH_LOG("[CRT]: Setting Aspect Ratio: %f \n", (float)p_switch->fly_aspect);

   video_driver_apply_state_changes();
   
}

static void set_aspect(videocrt_switch_t *p_switch, unsigned int width, 
      unsigned int height, unsigned int srm_width, unsigned srm_height,
      unsigned int srm_xscale, unsigned srm_yscale)
{
   int scaled_width = roundf(width*srm_xscale);
   int scaled_height = roundf(height*srm_yscale);

   crt_aspect_ratio_switch(p_switch, scaled_width, scaled_height);
}

static bool crt_sr2_init(unsigned int monitor_index, unsigned int crt_mode, unsigned int super_width)
{
   const char* err_msg;
   char* mode;
   char index = 0;
   char mindex[1];

   if (monitor_index+1 >= 0 && monitor_index+1 < 10)
      index = monitor_index+48;
   else
      index = '0';

   mindex[0] = index;

   if (!sr2_active)
   {

      dlp = OPENLIB(LIBSWR);

      /* Loading failed, inform and exit */
      if (!dlp) {
         RARCH_LOG("[CRT]: Switchres Library not found \n");
         
      }
      
      /* Load SR2 */
      LIBERROR();
      SRobj =  (srAPI*)LIBFUNC(dlp, "srlib");
      sr2_active = true;
      
      if ((err_msg = LIBERROR()) != NULL) 
      {
         CLOSELIB(dlp);  
         sr2_active = false;
         RARCH_LOG("[CRT]: Switchres Library failed to load \n");
      }else{
         RARCH_LOG("[CRT]: Switchres Library Loaded \n");
      }

      RARCH_LOG("[CRT]: SR init \n");
      SRobj->init();
      SRobj->sr_set_log_level (3);
      SRobj->sr_set_log_callback_info(RARCH_LOG);
      SRobj->sr_set_log_callback_debug(RARCH_LOG);
      SRobj->sr_set_log_callback_error(RARCH_LOG);

      if (crt_mode == 1)
      {
         SRobj->sr_set_monitor("arcade_15");
         RARCH_LOG("[CRT]: CRT Mode: %d - arcade_15 \n", crt_mode) ;
      }else if (crt_mode == 2)
      {
         SRobj->sr_set_monitor("arcade_31");
         RARCH_LOG("[CRT]: CRT Mode: %d - arcade_31 \n", crt_mode) ;
      }else if (crt_mode == 3)
      {
         SRobj->sr_set_monitor("pc_31_120");
         RARCH_LOG("[CRT]: CRT Mode: %d - pc_31_120 \n", crt_mode) ;
      }else if (crt_mode == 4)
      {
         RARCH_LOG("[CRT]: CRT Mode: %d - Seleted from ini \n", crt_mode) ;
      }


      if (super_width >2 )
         SRobj->sr_set_user_mode(super_width, 0, 0);
      
      RARCH_LOG("[CRT]: SR init_disp \n");
      if (monitor_index+1 > 0)
      {
         RARCH_LOG("SRobj: RA Monitor Index: %s\n",mindex);
         rtn = SRobj->sr_init_disp(mindex); 
         RARCH_LOG("[CRT]: SR Disp Monitor Index: %s  \n", mindex);
      }

      if (monitor_index == -1)
      {
         RARCH_LOG("SRobj: RA Monitor Index: %s\n",NULL);
         rtn = SRobj->sr_init_disp(NULL);
         RARCH_LOG("[CRT]: SR Disp Monitor Index: Auto  \n");
      }

      RARCH_LOG("[CRT]: SR rtn %d \n", rtn);

   }
   
   if (rtn == 1)
   {
     return true;
   }else{
      SRobj->deinit();
      sr2_active = false;
   } 

   return false;
}


static void switch_res_crt(
      videocrt_switch_t *p_switch,
      unsigned width, unsigned height, unsigned crt_mode, unsigned native_width, int monitor_index, int super_width)
{
   unsigned char interlace = 0,   ret;
   const char* err_msg;
   int w = native_width, h = height;
   double rr = p_switch->ra_core_hz;

     
   if (crt_sr2_init(monitor_index, crt_mode, super_width)) /* Checked SR2 is loded if not Load it */
   {
 
      ret =   SRobj->sr_switch_to_mode(w, h, rr, interlace, &srm);
      if(!ret) 
      {
            SRobj->deinit();
            
      }
      p_switch->ra_core_hz = srm.refresh;
      if (super_width >2 )
         set_aspect(p_switch, w , h, super_width, srm.height, srm.x_scale, srm.y_scale);
      else
         set_aspect(p_switch, w , h, srm.width, srm.height, srm.x_scale, srm.y_scale);
      
      RARCH_LOG("[CRT]: SR scaled  X:%d Y:%d \n",srm.x_scale, srm.y_scale);

   }else {
      set_aspect(p_switch, width , height, width, height ,1,1);
      video_driver_set_size(width , height); 
      video_driver_apply_state_changes();
      #if defined(HAVE_VIDEOCORE)
         crt_rpi_switch(width, height, p_switch->ra_core_hz, 0)
      #endif
   }
}

void crt_destroy_modes(videocrt_switch_t *p_switch)
{
   if (sr2_active == true)
   {
      if (SRobj)
      {   
         SRobj->deinit();
         CLOSELIB(dlp);
         RARCH_LOG("[CRT]: SR Destroyed \n");
      }
   }
}
/*
static int crt_compute_dynamic_width(
      videocrt_switch_t *p_switch,
      int width)
{
   unsigned i;
   int       dynamic_width     = 0;
   unsigned       min_height   = 261;

#if defined(HAVE_VIDEOCORE)
   p_switch->p_clock           = 32000000;
#else
   p_switch->p_clock           = 21000000;
#endif

   for (i = 0; i < 10; i++)
   {
      dynamic_width = width * i;
      if ((dynamic_width * min_height * p_switch->ra_core_hz) 
            > p_switch->p_clock)
         break;
   }
   RARCH_LOG("[CRT]: Dynamic Width Set to: %d \n", dynamic_width );
   return dynamic_width;
}
*/
void crt_switch_res_core(
      videocrt_switch_t *p_switch,
      unsigned native_width, unsigned width, unsigned height,
      float hz, unsigned crt_mode,
      int crt_switch_center_adjust,
      int crt_switch_porch_adjust,
      int monitor_index, bool dynamic,
      int super_width)
{
     
   if (height != 4 )
   {
      menu_active = false;
	   p_switch->porch_adjust          = crt_switch_porch_adjust;
      p_switch->ra_core_height        = height;
      p_switch->ra_core_hz            = hz;

      p_switch->ra_core_width         = width;

      p_switch->center_adjust         = crt_switch_center_adjust;
      p_switch->index                 = monitor_index;

      /* Detect resolution change and switch */
      if ( 
            (p_switch->ra_tmp_height != p_switch->ra_core_height) ||
            (p_switch->ra_core_width != p_switch->ra_tmp_width) || 
            (p_switch->center_adjust != p_switch->tmp_center_adjust||
             p_switch->porch_adjust  !=  p_switch->tmp_porch_adjust )
         )
      {
         RARCH_LOG("[CRT]: Requested Reolution: %dx%d@%f \n", native_width, height, hz);

         switch_res_crt(p_switch, p_switch->ra_core_width, p_switch->ra_core_height , crt_mode, native_width, monitor_index-1, super_width);

         if (p_switch->ra_core_hz != p_switch->ra_tmp_core_hz)
         {
            switch_crt_hz(p_switch);

         }
         
         p_switch->ra_tmp_height     = p_switch->ra_core_height;
         p_switch->ra_tmp_width      = p_switch->ra_core_width;
         p_switch->tmp_center_adjust = p_switch->center_adjust;
         p_switch->tmp_porch_adjust  = p_switch->porch_adjust;
      }

      if (video_driver_get_aspect_ratio() != p_switch->fly_aspect)
      {
         RARCH_LOG("[CRT]: Restoring Aspect Ratio: %f \n", (float)p_switch->fly_aspect);
         video_driver_set_aspect_ratio_value((float)p_switch->fly_aspect);
         video_driver_apply_state_changes();
      }

   }else{
      if (menu_active == false)
      {
         if (fb_width == 0)
         {
            video_driver_get_size(&fb_width, &fb_height);
            RARCH_LOG("[CRT]: Menu Only Dimentions: %dx%d \n", fb_width, fb_height);
            crt_aspect_ratio_switch(p_switch, fb_width, fb_height);
         }else{
            unsigned int tmp_fb_width = 0;
            unsigned int tmp_fb_height = 0;
            size_t tmp_pitch = 0;
            RARCH_LOG("[CRT]: Menu Only Dimentions restoring: %dx%d \n", fb_width, fb_height);
            switch_res_crt(p_switch, fb_width, fb_height , crt_mode, 
            fb_width, monitor_index-1, super_width);
         }
         menu_active = true;
      }
   }
}
/* only used for RPi3 */
#if defined(HAVE_VIDEOCORE)
static void crt_rpi_switch(int width, int height, float hz, int xoffset)
{
   char buffer[1024];
   VCHI_INSTANCE_T vchi_instance;
   VCHI_CONNECTION_T *vchi_connection  = NULL;
   static char output[250]             = {0};
   static char output1[250]            = {0};
   static char output2[250]            = {0};
   static char set_hdmi[250]           = {0};
   static char set_hdmi_timing[250]    = {0};
   int i                               = 0;
   int hfp                             = 0;
   int hsp                             = 0;
   int hbp                             = 0;
   int vfp                             = 0;
   int vsp                             = 0;
   int vbp                             = 0;
   int hmax                            = 0;
   int vmax                            = 0;
   int pdefault                        = 8;
   int pwidth                          = 0;
   int ip_flag                         = 0;
   float roundw                        = 0.0f;
   float roundh                        = 0.0f;
   float pixel_clock                   = 0.0f;

   if (height > 300)
      height = height/2;

   /* set core refresh from hz */
   video_monitor_set_refresh_rate(hz);

   /* following code is the mode line generator */
   hsp    = (width * 0.117) - (xoffset*4);
   if (width < 700)
   {
      hfp    = (width * 0.065);
      hbp  = width * 0.35-hsp-hfp;
   }
   else
   {
      hfp  = (width * 0.033) + (width / 112);
      hbp  = (width * 0.225) + (width /58);
      xoffset = xoffset*2;
   }
   
   hmax = hbp;

   if (height < 241)
      vmax = 261;
   if (height < 241 && hz > 56 && hz < 58)
      vmax = 280;
   if (height < 241 && hz < 55)
      vmax = 313;
   if (height > 250 && height < 260 && hz > 54)
      vmax = 296;
   if (height > 250 && height < 260 && hz > 52 && hz < 54)
      vmax = 285;
   if (height > 250 && height < 260 && hz < 52)
      vmax = 313;
   if (height > 260 && height < 300)
      vmax = 318;

   if (height > 400 && hz > 56)
      vmax = 533;
   if (height > 520 && hz < 57)
      vmax = 580;

   if (height > 300 && hz < 56)
      vmax = 615;
   if (height > 500 && hz < 56)
      vmax = 624;
   if (height > 300)
      pdefault = pdefault * 2;

   vfp = (height + ((vmax - height) / 2) - pdefault) - height;

   if (height < 300)
      vsp = vfp + 3; /* needs to be 3 for progressive */
   if (height > 300)
      vsp = vfp + 6; /* needs to be 6 for interlaced */

   vsp  = 3;
   vbp  = (vmax-height)-vsp-vfp;
   hmax = width+hfp+hsp+hbp;

   if (height < 300)
   {
      pixel_clock = (hmax * vmax * hz) ;
      ip_flag     = 0;
   }

   if (height > 300)
   {
      pixel_clock = (hmax * vmax * (hz/2)) /2 ;
      ip_flag     = 1;
   }
   /* above code is the modeline generator */

   snprintf(set_hdmi_timing, sizeof(set_hdmi_timing),
         "hdmi_timings %d 1 %d %d %d %d 1 %d %d %d 0 0 0 %f %d %f 1 ",
         width, hfp, hsp, hbp, height, vfp,vsp, vbp,
         hz, ip_flag, pixel_clock);

   vcos_init();

   vchi_initialise(&vchi_instance);

   vchi_connect(NULL, 0, vchi_instance);

   vc_vchi_gencmd_init(vchi_instance, &vchi_connection, 1);

   vc_gencmd(buffer, sizeof(buffer), set_hdmi_timing);

   vc_gencmd_stop();

   vchi_disconnect(vchi_instance);

   snprintf(output1,  sizeof(output1),
         "tvservice -e \"DMT 87\" > /dev/null");
   system(output1);
   snprintf(output2,  sizeof(output1),
         "fbset -g %d %d %d %d 24 > /dev/null",
         width, height, width, height);
   system(output2);
}
#endif

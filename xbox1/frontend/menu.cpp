/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2012 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2012 - Daniel De Matteis
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

#include "RetroLaunch/IoSupport.h"
#include "RetroLaunch/Surface.h"

#include "../../ps3/frontend/menu.h"
#include "../../console/fileio/file_browser.h"
#include "../../gfx/fonts/xdk1_xfonts.h"

#define NUM_ENTRY_PER_PAGE 17

#define ROM_PANEL_WIDTH 440
#define ROM_PANEL_HEIGHT 20

#define MAIN_TITLE_X 305
#define MAIN_TITLE_Y 30
#define MAIN_TITLE_COLOR 0xFFFFFFFF

#define MENU_MAIN_BG_X 0
#define MENU_MAIN_BG_Y 0

filebrowser_t browser;

// Rom selector panel with coords
d3d_surface_t m_menuMainRomSelectPanel;
// Background image with coords
d3d_surface_t m_menuMainBG;

// Rom list coords
int m_menuMainRomListPos_x;
int m_menuMainRomListPos_y;

// Backbuffer width, height
int width; 
int height;
wchar_t m_title[128];

static uint64_t old_state = 0;

typedef enum {
   MENU_ROMSELECT_ACTION_OK,
   MENU_ROMSELECT_ACTION_GOTO_SETTINGS,
   MENU_ROMSELECT_ACTION_NOOP,
} menu_romselect_action_t;

static void display_menubar(void)
{
   //Render background image
   d3d_surface_render(&m_menuMainBG, MENU_MAIN_BG_X, MENU_MAIN_BG_Y,
   m_menuMainBG.m_imageInfo.Width, m_menuMainBG.m_imageInfo.Height);
}

static void browser_update(filebrowser_t * b, uint16_t input, const char *extensions)
{
   filebrowser_action_t action = FILEBROWSER_ACTION_NOOP;

   if (input & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN))
      action = FILEBROWSER_ACTION_DOWN;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_UP))
      action = FILEBROWSER_ACTION_UP;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT))
      action = FILEBROWSER_ACTION_RIGHT;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT))
      action = FILEBROWSER_ACTION_LEFT;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_R))
      action = FILEBROWSER_ACTION_SCROLL_DOWN;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_L))
      action = FILEBROWSER_ACTION_SCROLL_UP;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_A))
      action = FILEBROWSER_ACTION_CANCEL;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_START))
   {
      action = FILEBROWSER_ACTION_RESET;
      filebrowser_set_root(b, "/");
      strlcpy(b->extensions, extensions, sizeof(b->extensions));
   }

   if(action != FILEBROWSER_ACTION_NOOP)
      filebrowser_iterate(b, action);
}

static void browser_render(filebrowser_t *b, float current_x, float current_y, float y_spacing)
{
   xdk_d3d_video_t *d3d = (xdk_d3d_video_t*)driver.video_data;
   unsigned file_count = b->current_dir.list->size;
   unsigned current_index, page_number, page_base, i;
   float currentX, currentY, ySpacing;

   current_index = b->current_dir.ptr;
   page_number = current_index / NUM_ENTRY_PER_PAGE;
   page_base = page_number * NUM_ENTRY_PER_PAGE;

   currentX = current_x;
   currentY = current_y;
   ySpacing = y_spacing;

   for (i = page_base; i < file_count && i < page_base + NUM_ENTRY_PER_PAGE; ++i)
   {
      char fname_tmp[256];
      fill_pathname_base(fname_tmp, b->current_dir.list->elems[i].data, sizeof(fname_tmp));
      currentY = currentY + ySpacing;

      const char *rom_basename = fname_tmp;
      wchar_t rom_basename_w[256];

      //check if this is the currently selected file
      const char *current_pathname = filebrowser_get_current_path(b);
      if(strcmp(current_pathname, b->current_dir.list->elems[i].data) == 0)
         d3d_surface_render(&m_menuMainRomSelectPanel, currentX, currentY, ROM_PANEL_WIDTH, ROM_PANEL_HEIGHT);

      convert_char_to_wchar(rom_basename_w, rom_basename, sizeof(rom_basename_w));
      xfonts_render_msg_pre(d3d);
      xfonts_render_msg_place(d3d, currentX, currentY, 0 /* scale */, rom_basename_w);
      xfonts_render_msg_post(d3d);
   }
}

static void menu_romselect_iterate(filebrowser_t *filebrowser, menu_romselect_action_t action)
{
   switch(action)
   {
      case MENU_ROMSELECT_ACTION_OK:
         if(filebrowser_get_current_path_isdir(filebrowser))
            filebrowser_iterate(filebrowser, FILEBROWSER_ACTION_OK);
         else
            rarch_console_load_game_wrap(filebrowser_get_current_path(filebrowser), g_console.zip_extract_mode, S_DELAY_45);
         break;
      case MENU_ROMSELECT_ACTION_GOTO_SETTINGS:
         break;
      default:
         break;
   }
}

static void select_rom(uint16_t input)
{
   xdk_d3d_video_t *d3d = (xdk_d3d_video_t*)driver.video_data;

   browser_update(&browser, input, rarch_console_get_rom_ext());
   
   menu_romselect_action_t action = MENU_ROMSELECT_ACTION_NOOP;
   
   if (input & (1 << RETRO_DEVICE_ID_JOYPAD_B))
      action = MENU_ROMSELECT_ACTION_OK;
   else if (input & (1 << RETRO_DEVICE_ID_JOYPAD_R3))
   {
      LD_LAUNCH_DASHBOARD LaunchData = { XLD_LAUNCH_DASHBOARD_MAIN_MENU };
      XLaunchNewImage( NULL, (LAUNCH_DATA*)&LaunchData );
   }

   if (action != MENU_ROMSELECT_ACTION_NOOP)
      menu_romselect_iterate(&browser, action);

   display_menubar();

   //Display some text
   //Center the text (hardcoded)
   int xpos = width == 640 ? 65 : 400;
   int ypos = width == 640 ? 430 : 670;
   
   xfonts_render_msg_pre(d3d);
   xfonts_render_msg_place(d3d, xpos, ypos, 0 /* scale */, m_title);
   xfonts_render_msg_post(d3d);
}

int menu_init(void)
{
   xdk_d3d_video_t *d3d = (xdk_d3d_video_t*)driver.video_data;

   // Set libretro filename and version to variable
   struct retro_system_info info;
   retro_get_system_info(&info);
   const char *id = info.library_name ? info.library_name : "Unknown";
   char core_text[256];
   snprintf(core_text, sizeof(core_text), "Libretro core: %s %s", id, info.library_version);
   convert_char_to_wchar(m_title, core_text, sizeof(m_title));

   // Set file cache size
   XSetFileCacheSize(8 * 1024 * 1024);

   // Mount drives
   xbox_io_mount("A:", "cdrom0");
   xbox_io_mount("E:", "Harddisk0\\Partition1");
   xbox_io_mount("Z:", "Harddisk0\\Partition2");
   xbox_io_mount("F:", "Harddisk0\\Partition6");
   xbox_io_mount("G:", "Harddisk0\\Partition7");

	strlcpy(browser.extensions, rarch_console_get_rom_ext(), sizeof(browser.extensions));
   filebrowser_set_root(&browser, g_console.default_rom_startup_dir);
   filebrowser_iterate(&browser, FILEBROWSER_ACTION_RESET);
   
   width  = d3d->d3dpp.BackBufferWidth;

   // Quick hack to properly center the romlist in 720p, 
   // it might need more work though (font size and rom selector size -> needs more memory)
   // Init rom list coords
   // Load background image
   if(width == 640)
   {
      d3d_surface_new(&m_menuMainBG, "D:\\Media\\menuMainBG.png");
      m_menuMainRomListPos_x = 100;
      m_menuMainRomListPos_y = 100;
   }
   else if(width == 1280)
   {
      d3d_surface_new(&m_menuMainBG, "D:\\Media\\menuMainBG_720p.png");
      m_menuMainRomListPos_x = 400;
      m_menuMainRomListPos_y = 150;
   }

   // Load rom selector panel
   d3d_surface_new(&m_menuMainRomSelectPanel, "D:\\Media\\menuMainRomSelectPanel.png");

   g_console.mode_switch = MODE_MENU;

   return 0;
}

void menu_free(void)
{
   filebrowser_free(&browser);
   d3d_surface_free(&m_menuMainBG);
   d3d_surface_free(&m_menuMainRomSelectPanel);
}

void menu_loop(void)
{
   DEVICE_CAST device_ptr = (DEVICE_CAST)driver.video_data;

   g_console.menu_enable = true;

   do
   {
      //first button input frame
      uint64_t input_state_first_frame = 0;
      uint64_t input_state = 0;
      static bool first_held = false;

       input_ptr.poll(NULL);

      static const struct retro_keybind *binds[MAX_PLAYERS] = {
	      g_settings.input.binds[0],
	      g_settings.input.binds[1],
	      g_settings.input.binds[2],
	      g_settings.input.binds[3],
	      g_settings.input.binds[4],
	      g_settings.input.binds[5],
	      g_settings.input.binds[6],
	      g_settings.input.binds[7],
      };

      static const struct retro_keybind _analog_binds[] = {
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_LEFT), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_RIGHT), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_UP), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_DOWN), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_LEFT), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_RIGHT), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_UP), 0 },
	      { 0, 0, (enum retro_key)0, (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_DOWN), 0 },
      };

      const struct retro_keybind *analog_binds[] = {
	      _analog_binds
      };

      for (unsigned i = 0; i < RARCH_FIRST_META_KEY; i++)
      {
         input_state |= input_ptr.input_state(NULL, binds, false,
            RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
      }

      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 0) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_LEFT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 1) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_RIGHT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 2) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_UP) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 3) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_DOWN) : 0;

      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 4) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_LEFT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 5) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_RIGHT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 6) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_UP) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 7) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_DOWN) : 0;

      uint64_t trig_state = input_state & ~old_state; //set first button input frame as trigger
      input_state_first_frame = input_state;          //hold onto first button input frame

      //second button input frame
      input_state = 0;
      input_ptr.poll(NULL);


      for (unsigned i = 0; i < RARCH_FIRST_META_KEY; i++)
      {
         input_state |= input_ptr.input_state(NULL, binds, false,
            RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
      }

      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 0) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_LEFT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 1) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_RIGHT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 2) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_UP) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 3) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_DOWN) : 0;

      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 4) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_LEFT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 5) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_RIGHT) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 6) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_UP) : 0;
      input_state |= input_ptr.input_state(NULL, analog_binds, false,
         RETRO_DEVICE_JOYPAD, 0, 7) ? (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_DOWN) : 0;

      bool analog_sticks_pressed = (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_LEFT)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_RIGHT)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_UP)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_LEFT_DPAD_DOWN)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_LEFT)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_RIGHT)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_UP)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_ANALOG_RIGHT_DPAD_DOWN));
      bool shoulder_buttons_pressed = ((input_state & (1 << RETRO_DEVICE_ID_JOYPAD_L2)) || (input_state & (1 << RETRO_DEVICE_ID_JOYPAD_R2))) /*&& current_menu->category_id != CATEGORY_SETTINGS*/;
      bool do_held = analog_sticks_pressed || shoulder_buttons_pressed;

      if(do_held)
      {
         if(!first_held)
         {
            first_held = true;
            SET_TIMER_EXPIRATION(device_ptr, 7);
         }
         
         if(IS_TIMER_EXPIRED(device_ptr))
         {
            first_held = false;
            trig_state = input_state; //second input frame set as current frame
         }
      }
      
      device_ptr->d3d_render_device->Clear(0, NULL, D3DCLEAR_TARGET,
         D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
      device_ptr->frame_count++;

      device_ptr->d3d_render_device->BeginScene();
      device_ptr->d3d_render_device->SetFlickerFilter(1);
      device_ptr->d3d_render_device->SetSoftDisplayFilter(1);
      
      select_rom(trig_state);
      browser_render(&browser, m_menuMainRomListPos_x, m_menuMainRomListPos_y, 20);
      
      old_state = input_state_first_frame;

      if(IS_TIMER_EXPIRED(device_ptr))
      {
         // if we want to force goto the emulation loop, skip this
         if(g_console.mode_switch != MODE_EMULATION)
         {
            // for ingame menu, we need a different precondition because menu_enable
            // can be set to false when going back from ingame menu to menu
            if(g_console.ingame_menu_enable == true)
            {
               //we want to force exit when mode_switch is set to MODE_EXIT
               if(g_console.mode_switch != MODE_EXIT)
                  g_console.mode_switch = (((old_state & (1 << RETRO_DEVICE_ID_JOYPAD_L3)) && (old_state & (1 << RETRO_DEVICE_ID_JOYPAD_R3)) && g_console.emulator_initialized)) ? MODE_EMULATION : MODE_MENU;
            }
            else
            {
               g_console.menu_enable = !(((old_state & (1 << RETRO_DEVICE_ID_JOYPAD_L3)) && (old_state & (1 << RETRO_DEVICE_ID_JOYPAD_R3)) && g_console.emulator_initialized));
               g_console.mode_switch = g_console.menu_enable ? MODE_MENU : MODE_EMULATION;
            }
         }
      }

      // set a timer delay so that we don't instantly switch back to the menu when
      // press and holding L3 + R3 in the emulation loop (lasts for 30 frame ticks)
      if(g_console.mode_switch == MODE_EMULATION && !g_console.frame_advance_enable)
      {
         SET_TIMER_EXPIRATION(device_ptr, 30);
      }
      
      device_ptr->d3d_render_device->EndScene();
      device_ptr->d3d_render_device->Present(NULL, NULL, NULL, NULL);
   }while(g_console.menu_enable);

   g_console.ingame_menu_enable = false;
}

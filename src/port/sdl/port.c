/*
 * FrontEnd code for PCSX4ALL
 * License : GPLv2
 * Authors : Senquack, Dmitry Smagin, Gameblabla, JamesOFarrel, jbd1986 
*/

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <SDL.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <sys/ioctl.h>
#endif

#ifdef RUMBLE
#include <shake.h>
Shake_Device *device;
Shake_Effect effect;
int id_shake;
#endif

#include "port.h"
#include "r3000a.h"
#include "plugins.h"
#include "plugin_lib.h"
#include "perfmon.h"
#include "cdrom_hacks.h"

/* PATH_MAX inclusion */
#ifdef __MINGW32__
  #include <limits.h>
#endif

#ifdef SPU_PCSXREARMED
  #include "spu/spu_pcsxrearmed/spu_config.h"		// To set spu-specific configuration
#endif

// New gpulib from Notaz's PCSX Rearmed handles duties common to GPU plugins
#ifdef USE_GPULIB
  #include "gpu/gpulib/gpu.h"
#endif

#ifdef GPU_UNAI
  #include "gpu/gpu_unai/gpu.h"
#endif

enum
{
  DKEY_SELECT = 0,
  DKEY_L3 = 1,
  DKEY_R3 = 2,
  DKEY_START = 3,
  DKEY_UP = 4,
  DKEY_RIGHT = 5,
  DKEY_DOWN = 6,
  DKEY_LEFT = 7,
  DKEY_L2 = 8,
  DKEY_R2 = 9,
  DKEY_L1 = 10,
  DKEY_R1 =  11,
  DKEY_TRIANGLE = 12,
  DKEY_CIRCLE = 13,
  DKEY_CROSS = 14,
  DKEY_SQUARE= 15,

  DKEY_TOTAL = 16
};

static SDL_Surface *screen;
unsigned short *SCREEN;

static uint8_t pcsx4all_initted = 0;
static uint8_t emu_running = 0;

void config_load();
void config_save();

static void pcsx4all_exit(void)
{
	// Store config to file
	config_save();

	if (SDL_MUSTLOCK(screen))
		SDL_UnlockSurface(screen);

	SDL_Quit();

#ifdef RUMBLE
	Shake_Stop(device, id_shake);
	Shake_EraseEffect(device, id_shake);
	Shake_Close(device);
	Shake_Quit();
#endif

	if (pcsx4all_initted == 1)
	{
		ReleasePlugins();
		psxShutdown();
	}
}

static char homedir[PATH_MAX/2];
static char memcardsdir[PATH_MAX];
static char biosdir[PATH_MAX];
static char patchesdir[PATH_MAX];
char sstatesdir[PATH_MAX];

#ifdef __WIN32__
	#define MKDIR(A) mkdir(A)
#else
	#define MKDIR(A) mkdir(A, 0777)
#endif

static void setup_paths()
{
#ifndef __WIN32__
	snprintf(homedir, sizeof(homedir), "%s/.pcsx4all", getenv("HOME"));
#else
	char buf[PATH_MAX];
	snprintf(homedir, sizeof(homedir), "%s/.pcsx4all", getcwd(buf, PATH_MAX));
#endif
	
	/* 
	 * If folder does not exists then create it 
	 * This can speeds up startup if the folder already exists
	*/

	if(access( homedir, F_OK ) != -1) 
	{
		snprintf(sstatesdir, sizeof(sstatesdir), "%s/sstates", homedir);
		snprintf(memcardsdir, sizeof(memcardsdir), "%s/memcards", homedir);
		snprintf(biosdir, sizeof(biosdir), "%s/bios", homedir);
		snprintf(patchesdir, sizeof(patchesdir), "%s/patches", homedir);
	}
	
	MKDIR(homedir);
	MKDIR(sstatesdir);
	MKDIR(memcardsdir);
	MKDIR(biosdir);
	MKDIR(patchesdir);
}

void probe_lastdir()
{
  DIR *dir;
 /* if (!Config.LastDir)
    return;*/

  dir = opendir(Config.LastDir);

  if (!dir)
  {
    // Fallback to home directory.
    strncpy(Config.LastDir, homedir, MAXPATHLEN);
    Config.LastDir[MAXPATHLEN-1] = '\0';
  }
  else
  {
    closedir(dir);
  }
}

#ifdef PSXREC
  extern u32 cycle_multiplier; // in mips/recompiler.cpp
#endif

void config_load()
{
  FILE *f;
  char config[PATH_MAX];
  char line[strlen("LastDir ") + MAXPATHLEN + 1];
  int lineNum = 0;

  snprintf(config, sizeof(config), "%s/pcsx4all.cfg", homedir);

  f = fopen(config, "r");

  if (f == NULL)
  {
    printf("Failed to open config file: \"%s\" for reading.\n", config);
    return;
  }

  while (fgets(line, sizeof(line), f))
  {
    char *arg = strchr(line, ' ');
    int value;

    ++lineNum;

    if (!arg)
    {
      continue;
    }

    *arg = '\0';
    arg++;

    if (lineNum == 1)
    {
      if (!strcmp(line, "CONFIG_VERSION"))
      {
        sscanf(arg, "%d", &value);
        if (value == CONFIG_VERSION)
        {
          continue;
        }
        else
        {
          printf("Incompatible config version for \"%s\"."
                 "Required: %d. Found: %d. Ignoring.\n",
                 config, CONFIG_VERSION, value);
          break;
        }
      }

      printf("Incompatible config format for \"%s\"."
             "Ignoring.\n", config);
      break;
    }

    if (!strcmp(line, "Xa"))
    {
      sscanf(arg, "%d", &value);
      Config.Xa = value;
    }
    else if (!strcmp(line, "Mdec"))
    {
      sscanf(arg, "%d", &value);
      Config.Mdec = value;
    }
    else if (!strcmp(line, "PsxAuto"))
    {
      sscanf(arg, "%d", &value);
      Config.PsxAuto = value;
    }
    else if (!strcmp(line, "Cdda"))
    {
      sscanf(arg, "%d", &value);
      Config.Cdda = value;
    }
    else if (!strcmp(line, "HLE"))
    {
      sscanf(arg, "%d", &value);
      Config.HLE = value;
    }
    else if (!strcmp(line, "RCntFix"))
    {
      sscanf(arg, "%d", &value);
      Config.RCntFix = value;
    }
    else if (!strcmp(line, "VSyncWA"))
    {
      sscanf(arg, "%d", &value);
      Config.VSyncWA = value;
    }
    else if (!strcmp(line, "Cpu"))
    {
      sscanf(arg, "%d", &value);
      Config.Cpu = value;
    }
    else if (!strcmp(line, "PsxType"))
    {
      sscanf(arg, "%d", &value);
      Config.PsxType = value;
    }
    else if (!strcmp(line, "SpuIrq"))
    {
      sscanf(arg, "%d", &value);
      Config.SpuIrq = value;
    }
    else if (!strcmp(line, "SyncAudio"))
    {
      sscanf(arg, "%d", &value);
      Config.SyncAudio = value;
    }
    else if (!strcmp(line, "SpuUpdateFreq"))
    {
      sscanf(arg, "%d", &value);
      if (value < SPU_UPDATE_FREQ_MIN || value > SPU_UPDATE_FREQ_MAX)
        value = SPU_UPDATE_FREQ_DEFAULT;
      Config.SpuUpdateFreq = value;
    }
    else if (!strcmp(line, "ForcedXAUpdates"))
    {
      sscanf(arg, "%d", &value);
      if (value < FORCED_XA_UPDATES_MIN || value > FORCED_XA_UPDATES_MAX)
        value = FORCED_XA_UPDATES_DEFAULT;
      Config.ForcedXAUpdates = value;
    }
    else if (!strcmp(line, "ShowFps"))
    {
      sscanf(arg, "%d", &value);
      Config.ShowFps = value;
    }
    else if (!strcmp(line, "FrameLimit"))
    {
      sscanf(arg, "%d", &value);
      Config.FrameLimit = value;
    }
    else if (!strcmp(line, "FrameSkip"))
    {
      sscanf(arg, "%d", &value);
      if (value < FRAMESKIP_MIN || value > FRAMESKIP_MAX)
        value = FRAMESKIP_OFF;
      Config.FrameSkip = value;
    }
    else if (!strcmp(line, "AnalogArrow"))
    {
      sscanf(arg, "%d", &value);
      Config.AnalogArrow = value;
    }
    else if (!strcmp(line, "Analog_Mode"))
    {
      sscanf(arg, "%d", &value);
      Config.Analog_Mode = value;
    }
    else if (!strcmp(line, "SlowBoot"))
    {
      sscanf(arg, "%d", &value);
      Config.SlowBoot = value;
    }
    else if (!strcmp(line, "McdSlot1"))
    {
      sscanf(arg, "%d", &value);
      Config.McdSlot1 = value;
    }
    else if (!strcmp(line, "McdSlot2"))
    {
      sscanf(arg, "%d", &value);
      Config.McdSlot2 = value;
    }
#ifdef SPU_PCSXREARMED
    else if (!strcmp(line, "SpuUseInterpolation"))
    {
      sscanf(arg, "%d", &value);
      spu_config.iUseInterpolation = value;
    }
    else if (!strcmp(line, "SpuUseReverb"))
    {
      sscanf(arg, "%d", &value);
      spu_config.iUseReverb = value;
    }
    else if (!strcmp(line, "SpuVolume"))
    {
      sscanf(arg, "%d", &value);
      if (value > 1024) value = 1024;
      if (value < 0) value = 0;
      spu_config.iVolume = value;
    }
#endif
    else if (!strcmp(line, "LastDir"))
    {
      int len = strlen(arg);

      if (len == 0 || len > sizeof(Config.LastDir) - 1)
      {
        continue;
      }

      if (arg[len-1] == '\n')
      {
        arg[len-1] = '\0';
      }

      strcpy(Config.LastDir, arg);
    }
    else if (!strcmp(line, "BiosDir"))
    {
      int len = strlen(arg);

      if (len == 0 || len > sizeof(Config.BiosDir) - 1)
      {
        continue;
      }

      if (arg[len-1] == '\n')
      {
        arg[len-1] = '\0';
      }

      strcpy(Config.BiosDir, arg);
    }
    else if (!strcmp(line, "Bios"))
    {
      int len = strlen(arg);

      if (len == 0 || len > sizeof(Config.Bios) - 1)
      {
        continue;
      }

      if (arg[len-1] == '\n')
      {
        arg[len-1] = '\0';
      }

      strcpy(Config.Bios, arg);
    }
#ifdef PSXREC
    else if (!strcmp(line, "CycleMultiplier"))
    {
      sscanf(arg, "%03x", &value);
      cycle_multiplier = value;
    }
#endif
    else if (!strcmp(line, "Blit512Mode"))
    {
      sscanf(arg, "%d", &value);
      Config.Blit512Mode = value;
    }
    else if (!strcmp(line, "Blit480H"))
    {
      sscanf(arg, "%d", &value);
      Config.Blit480H = value;
    }
    else if (!strcmp(line, "Blit256W"))
    {
      sscanf(arg, "%d", &value);
      Config.Blit256W = value;
    }
    else if (!strcmp(line, "Blit368W"))
    {
      sscanf(arg, "%d", &value);
      Config.Blit368W = value;
    }
#ifdef GPU_UNAI
    else if (!strcmp(line, "clip_368"))
    {
      sscanf(arg, "%d", &value);
      gpu_unai_config_ext.clip_368 = value;
    }
    else if (!strcmp(line, "lighting"))
    {
      sscanf(arg, "%d", &value);
      gpu_unai_config_ext.lighting = value;
    }
    else if (!strcmp(line, "fast_lighting"))
    {
      sscanf(arg, "%d", &value);
      gpu_unai_config_ext.fast_lighting = value;
    }
    else if (!strcmp(line, "blending"))
    {
      sscanf(arg, "%d", &value);
      gpu_unai_config_ext.blending = value;
    }
    else if (!strcmp(line, "dithering"))
    {
      sscanf(arg, "%d", &value);
      gpu_unai_config_ext.dithering = value;
    }
    else if (!strcmp(line, "interlace"))
    {
      sscanf(arg, "%d", &value);
      gpu_unai_config_ext.ilace_force = value;
    }
#endif
  }
#ifdef GPU_UNAI
  gpu_unai_config_ext.pixel_skip = Config.Blit512Mode == 1 ? 1 : 0;
#endif

  fclose(f);
}

void config_save()
{
  FILE *f;
  char config[PATH_MAX];

  snprintf(config, sizeof(config), "%s/pcsx4all.cfg", homedir);

  f = fopen(config, "w");

  if (f == NULL)
  {
    printf("Failed to open config file: \"%s\" for writing.\n", config);
    return;
  }

  fprintf(f, "CONFIG_VERSION %d\n"
          "Xa %d\n"
          "Mdec %d\n"
          "PsxAuto %d\n"
          "Cdda %d\n"
          "HLE %d\n"
          "RCntFix %d\n"
          "VSyncWA %d\n"
          "Cpu %d\n"
          "PsxType %d\n"
          "SpuIrq %d\n"
          "SyncAudio %d\n"
          "SpuUpdateFreq %d\n"
          "ForcedXAUpdates %d\n"
          "ShowFps %d\n"
          "FrameLimit %d\n"
          "FrameSkip %d\n"
          "AnalogArrow %d\n"
          "Analog_Mode %d\n"
          "SlowBoot %d\n"
          "Blit512Mode %d\n"
          "Blit480H %d\n"
          "Blit256W %d\n"
          "Blit368W %d\n"
          "McdSlot1 %d\n"
          "McdSlot2 %d\n",
          CONFIG_VERSION, Config.Xa, Config.Mdec, Config.PsxAuto,
          Config.Cdda, Config.HLE, Config.RCntFix, Config.VSyncWA,
          Config.Cpu, Config.PsxType, Config.SpuIrq, Config.SyncAudio,
          Config.SpuUpdateFreq, Config.ForcedXAUpdates, Config.ShowFps, Config.FrameLimit,
          Config.FrameSkip, Config.AnalogArrow, Config.Analog_Mode, Config.SlowBoot,
          Config.Blit512Mode, Config.Blit480H, Config.Blit256W, Config.Blit368W,
          Config.McdSlot1, Config.McdSlot2);

#ifdef SPU_PCSXREARMED
  fprintf(f, "SpuUseInterpolation %d\n", spu_config.iUseInterpolation);
  fprintf(f, "SpuUseReverb %d\n", spu_config.iUseReverb);
  fprintf(f, "SpuVolume %d\n", spu_config.iVolume);
#endif

#ifdef PSXREC
  fprintf(f, "CycleMultiplier %03x\n", cycle_multiplier);
#endif

#ifdef GPU_UNAI
  fprintf(f, "interlace %d\n"
          "clip_368 %d\n"
          "lighting %d\n"
          "fast_lighting %d\n"
          "blending %d\n"
          "dithering %d\n",
          gpu_unai_config_ext.ilace_force,
          gpu_unai_config_ext.clip_368,
          gpu_unai_config_ext.lighting,
          gpu_unai_config_ext.fast_lighting,
          gpu_unai_config_ext.blending,
          gpu_unai_config_ext.dithering);
#endif


  if (Config.LastDir[0])
  {
    fprintf(f, "LastDir %s\n", Config.LastDir);
  }

  if (Config.BiosDir[0])
  {
    fprintf(f, "BiosDir %s\n", Config.BiosDir);
  }

  if (Config.Bios[0])
  {
    fprintf(f, "Bios %s\n", Config.Bios);
  }

  fsync(fileno(f));
  fclose(f);
}

// Returns 0: success, -1: failure
int state_load(int slot)
{
  char savename[512+PATH_MAX];
  snprintf(savename, sizeof(savename), "%s/%s.%d.sav", sstatesdir, CdromId, slot);

  if (FileExists(savename))
  {
    return LoadState(savename);
  }

  return -1;
}

// Returns 0: success, -1: failure
int state_save(int slot)
{
	char savename[512+PATH_MAX];
	snprintf(savename, sizeof(savename), "%s/%s.%d.sav", sstatesdir, CdromId, slot);
	return SaveState(savename);
}

static struct
{
  int key;
  int bit;
} keymap[] =
{
  { SDLK_UP,		DKEY_UP },
  { SDLK_DOWN,		DKEY_DOWN },
  { SDLK_LEFT,		DKEY_LEFT },
  { SDLK_RIGHT,		DKEY_RIGHT },
  
  { SDLK_LSHIFT,		DKEY_SQUARE },
  { SDLK_LCTRL,		DKEY_CIRCLE },
  { SDLK_SPACE,		DKEY_TRIANGLE },
  { SDLK_LALT,		DKEY_CROSS },
  { SDLK_TAB,		DKEY_L1 },
  { SDLK_BACKSPACE,	DKEY_R1 },
  { SDLK_PAGEUP,		DKEY_L2 },
  { SDLK_PAGEDOWN,			DKEY_R2 },
  { SDLK_KP_DIVIDE,		DKEY_L3 },
  { SDLK_KP_PERIOD,			DKEY_R3 },
  { SDLK_ESCAPE,		DKEY_SELECT },

  { SDLK_RETURN,		DKEY_START },
  { 0, 0 }
};

static uint16_t pad1 = 0xFFFF;
static uint16_t pad2 = 0xFFFF;

static uint16_t pad1_buttons = 0xFFFF;

static unsigned short analog1 = 0;
static int menu_check = 0;
uint8_t use_speedup = 0;
SDL_Joystick * sdl_joy[2];
#define joy_commit_range    2048
enum
{
  ANALOG_UP = 1,
  ANALOG_DOWN = 2,
  ANALOG_LEFT = 4,
  ANALOG_RIGHT = 8
};

struct ps1_controller player_controller[2];

void Set_Controller_Mode()
{
	switch(Config.Analog_Mode)
	{
		/* Digital. Required for some games. */
		default:
			player_controller[0].id = 0x41;
			player_controller[0].pad_mode = 0;
			player_controller[0].pad_controllertype = 0;
		break;
		/* DualAnalog. Some games might misbehave with Dualshock like Descent so this is for those */
		case 1:
			player_controller[0].id = 0x53;
			player_controller[0].pad_mode = 1;
			player_controller[0].pad_controllertype = 1;
		break;
		/* DualShock, required for Ape Escape. */
		case 2:
			player_controller[0].id = 0x73;
			player_controller[0].pad_mode = 1;
			player_controller[0].pad_controllertype = 1;
		break;
	}	
}

void joy_init(void)
{
	sdl_joy[0] = SDL_JoystickOpen(0);
	sdl_joy[1] = SDL_JoystickOpen(1);
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	SDL_JoystickEventState(SDL_ENABLE);
	
	player_controller[0].id = 0x41;
	player_controller[0].joy_left_ax0 = 127;
	player_controller[0].joy_left_ax1 = 127;
	player_controller[0].joy_right_ax0 = 127;
	player_controller[0].joy_right_ax1 = 127;
	
	player_controller[0].Vib[0] = 0;
	player_controller[0].Vib[1] = 0;
	player_controller[0].VibF[0] = 0;
	player_controller[0].VibF[1] = 0;
	
	player_controller[0].pad_mode = 0;
	player_controller[0].pad_controllertype = 0;
	
	player_controller[0].configmode = 0;
	
	Set_Controller_Mode();
}

void pad_update(void)
{
	int k = 0;
	int axisval;
	SDL_Event event;
	Uint8 *keys = SDL_GetKeyState(NULL);
	
	while (keymap[k].key)
	{
		if (keys[keymap[k].key])
		{
			pad1_buttons &= ~(1 << keymap[k].bit);
		}
		else
		{
			pad1_buttons |= (1 << keymap[k].bit);
		}
		k++;
	}

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_QUIT:
				pcsx4all_exit();
			break;
			case SDL_KEYDOWN:
			switch (event.key.keysym.sym)
			{
				case SDLK_HOME:
					menu_check = 1;
				break;
				case SDLK_v:
				{
					Config.ShowFps=!Config.ShowFps;
				}
				break;
				default:
				break;
			}
			
			case SDL_KEYUP:
			switch (event.key.keysym.sym)
			{
				case SDLK_HOME:
					menu_check = 1;
				break;
				default:
				break;
			}
			break;
			break;
			case SDL_JOYAXISMOTION:
			switch (event.jaxis.axis)
			{
			case 0: /* X axis */
				axisval = event.jaxis.value;
				if (Config.AnalogArrow == 1) 
				{
					analog1 &= ~(ANALOG_LEFT | ANALOG_RIGHT);
					if (axisval > joy_commit_range)
					{
						analog1 |= ANALOG_RIGHT;
					}
					else if (axisval < -joy_commit_range)
					{
						analog1 |= ANALOG_LEFT;
					}
				}
				else
				{
					player_controller[0].joy_left_ax0 = (axisval + 32768) / 256;
				}
			break;
			case 1: /* Y axis */
				axisval = event.jaxis.value;
				if (Config.AnalogArrow == 1) 
				{
					analog1 &= ~(ANALOG_UP | ANALOG_DOWN);
					if (axisval > joy_commit_range)
					{
						analog1 |= ANALOG_DOWN;
					}
					else if (axisval < -joy_commit_range)
					{
						analog1 |= ANALOG_UP;
					}
				} 
				else 
				{
					player_controller[0].joy_left_ax1 = (axisval + 32768) / 256;
				}
			break;
			case 2: /* X axis */
			axisval = event.jaxis.value;
			if (Config.AnalogArrow == 1) 
			{
				if (axisval > joy_commit_range)
				{
					pad1_buttons &= ~(1 << DKEY_CIRCLE);
				}
				else if (axisval < -joy_commit_range)
				{
					pad1_buttons &= ~(1 << DKEY_SQUARE);
				}
			} 
			else 
			{
				player_controller[0].joy_right_ax0 = (axisval + 32768) / 256;
			}
			break;
			case 3: /* Y axis */
			axisval = event.jaxis.value;
			if (Config.AnalogArrow == 1) 
			{
				if (axisval > joy_commit_range)
				{
					pad1_buttons &= ~(1 << DKEY_CROSS);
				}
				else if (axisval < -joy_commit_range)
				{
					pad1_buttons &= ~(1 << DKEY_TRIANGLE);
				}
			} 
			else
			{
				player_controller[0].joy_right_ax1 = (axisval + 32768) / 256;
			}
			break;
			}
			break;
			case SDL_JOYBUTTONDOWN:
			if(event.jbutton.which == 0) 
			{
				pad1_buttons |= (1 << DKEY_L3);
			}
			else if(event.jbutton.which == 1)
			{
				pad1_buttons |= (1 << DKEY_R3);
			}
			break;
		  default:
			break;
    }
  }

	/* Special key combos for GCW-Zero */
#ifdef GCW_ZERO
	if (Config.AnalogArrow == 1)
	{
		if ((pad1_buttons & (1 << DKEY_UP)) && (analog1 & ANALOG_UP))
		{
			pad1_buttons &= ~(1 << DKEY_UP);
		}
		if ((pad1_buttons & (1 << DKEY_DOWN)) && (analog1 & ANALOG_DOWN))
		{
			pad1_buttons &= ~(1 << DKEY_DOWN);
		}
		if ((pad1_buttons & (1 << DKEY_LEFT)) && (analog1 & ANALOG_LEFT))
		{
			pad1_buttons &= ~(1 << DKEY_LEFT);
		}
		if ((pad1_buttons & (1 << DKEY_RIGHT)) && (analog1 & ANALOG_RIGHT))
		{
			pad1_buttons &= ~(1 << DKEY_RIGHT);
		}
	}
  
  
	// SELECT+START for menu
	if (menu_check == 1)
	{
		// Sync and close any memcard files opened for writing
		// TODO: Disallow entering menu until they are synced/closed
		// automatically, displaying message that write is in progress.
		sioSyncMcds();

		emu_running = 0;
		pl_pause();    // Tell plugin_lib we're pausing emu
		GameMenu();
		emu_running = 1;
		use_speedup = 0;
		menu_check = 0;
		analog1 = 0;
		pad1_buttons |= (1 << DKEY_START) | (1 << DKEY_CROSS) | (1 << DKEY_SELECT);
		video_clear();
		video_flip();
		video_clear();
#ifdef SDL_TRIPLEBUF
		video_flip();
		video_clear();
#endif
		pl_resume();    // Tell plugin_lib we're reentering emu
	}
#endif

	pad1 = pad1_buttons;
}

uint16_t pad_read(int num)
{
	return (num == 0 ? pad1 : pad2);
}

void video_flip(void)
{
	if(emu_running && Config.ShowFps)
  {
    port_printf(5, 5, pl_data.stats_msg);
  }

  if(SDL_MUSTLOCK(screen))
  {
    SDL_UnlockSurface(screen);
  }

  SDL_Flip(screen);

  if(SDL_MUSTLOCK(screen))
  {
    SDL_LockSurface(screen);
  }
  SCREEN = (Uint16 *)screen->pixels;
}

/* This is used by gpu_dfxvideo only as it doesn't scale itself */
#ifdef GPU_DFXVIDEO
void video_set(unsigned short *pVideo, unsigned int width, unsigned int height)
{
  int y;
  unsigned short *ptr = SCREEN;
  int w = (width > 320 ? 320 : width);
  int h = (height > 240 ? 240 : height);

  for (y = 0; y < h; y++)
  {
    memcpy(ptr, pVideo, w * 2);
    ptr += 320;
    pVideo += width;
  }

  video_flip();
}
#endif

void video_clear(void)
{
  memset(screen->pixels, 0, screen->pitch*screen->h);
}

void update_mcd_fname(int load_mcd)
{
  sprintf(Config.McdPath1, "%s/mcd%03d.mcr", memcardsdir, (int)Config.McdSlot1);
  sprintf(Config.McdPath2, "%s/mcd%03d.mcr", memcardsdir, (int)Config.McdSlot2);
  if (load_mcd != 0)
  {
    LoadMcd(MCD1, Config.McdPath1); //Memcard 1
    LoadMcd(MCD2, Config.McdPath2); //Memcard 2
  }
}

// if [CdromId].bin is exsit, use the spec bios
int check_spec_bios(void)
{
  FILE *f = NULL;
  char bios[MAXPATHLEN];
  Config.BiosSpec[0] = 0;
  if (snprintf(bios, MAXPATHLEN, "%s/%s.bin", Config.BiosDir, CdromId) < MAXPATHLEN)
  {
    f = fopen(bios, "rb");
    if (f != NULL)
    {
      fclose(f);
      strcpy(Config.BiosSpec, CdromId);
      strcat(Config.BiosSpec, ".bin");
      return 1;
    }
  }
  return 0;
}


/* This is needed to override redirecting to stderr.txt and stdout.txt
with mingw build. */
#ifdef UNDEF_MAIN
  #undef main
#endif

void Rumble_Init()
{
#ifdef RUMBLE
	Shake_Init();

	if (Shake_NumOfDevices() > 0)
	{
		device = Shake_Open(0);
		Shake_InitEffect(&effect, SHAKE_EFFECT_PERIODIC);
		effect.u.periodic.waveform		= SHAKE_PERIODIC_SINE;
		effect.u.periodic.period		= 0.1*0x100;
		effect.u.periodic.magnitude		= 0x6000;
		effect.u.periodic.envelope.attackLength	= 0x100;
		effect.u.periodic.envelope.attackLevel	= 0;
		effect.u.periodic.envelope.fadeLength	= 0x100;
		effect.u.periodic.envelope.fadeLevel	= 0;
		effect.direction			= 0x4000;
		effect.length				= 0;
		effect.delay				= 0;
		id_shake = Shake_UploadEffect(device, &effect);
	}
#endif	
}

int main (int argc, char **argv)
{
  char filename[256];
  const char *cdrfilename = GetIsoFile();

  filename[0] = '\0'; /* Executable file name */

  setup_paths();
  

  // PCSX
  Config.McdSlot1 = 1;
  Config.McdSlot2 = 2;
  update_mcd_fname(0);
  strcpy(Config.PatchesDir, patchesdir);
  strcpy(Config.BiosDir, biosdir);
  strcpy(Config.Bios, "scph1001.bin");
  Config.BiosSpec[0] = 0;

  Config.Xa=0; /* 0=XA enabled, 1=XA disabled */
  Config.Mdec=0; /* 0=Black&White Mdecs Only Disabled, 1=Black&White Mdecs Only Enabled */
  Config.PsxAuto=1; /* 1=autodetect system (pal or ntsc) */
  Config.PsxType=0; /* PSX_TYPE_NTSC=ntsc, PSX_TYPE_PAL=pal */
  Config.Cdda=0; /* 0=Enable Cd audio, 1=Disable Cd audio */
  Config.HLE=1; /* 0=BIOS, 1=HLE */
#if defined (PSXREC)
  Config.Cpu=0; /* 0=recompiler, 1=interpreter */
#else
  Config.Cpu=1; /* 0=recompiler, 1=interpreter */
#endif
  Config.RCntFix=0; /* 1=Parasite Eve 2, Vandal Hearts 1/2 Fix */
  Config.VSyncWA=0; /* 1=InuYasha Sengoku Battle Fix */
  Config.SpuIrq=0; /* 1=SPU IRQ always on, fixes some games */
  Config.MemoryCardHack=0; /* Hack for Codename Tenka, only enabled for the game */

  Config.SyncAudio=0;	/* 1=emu waits if audio output buffer is full
	                       (happens seldom with new auto frame limit) */

  // Number of times per frame to update SPU. Rearmed default is once per
  //  frame, but we are more flexible (for slower devices).
  //  Valid values: SPU_UPDATE_FREQ_1 .. SPU_UPDATE_FREQ_32
  Config.SpuUpdateFreq = SPU_UPDATE_FREQ_DEFAULT;

  //senquack - Added option to allow queuing CDREAD_INT interrupts sooner
  //           than they'd normally be issued when SPU's XA buffer is not
  //           full. This fixes droupouts in music/speech on slow devices.
  Config.ForcedXAUpdates = FORCED_XA_UPDATES_DEFAULT;

  Config.ShowFps=0;    // 0=don't show FPS
  Config.FrameLimit = 1;
  Config.FrameSkip = FRAMESKIP_OFF;
  Config.AnalogArrow = 0;
  Config.Analog_Mode = 0;
  Config.Blit512Mode = 3; //0 ~ 3: old1 old2 new1 new2
  Config.Blit480H = 1; //0:skip half lines, 1:filter lines
  Config.Blit256W = 1; //0:old, 1:make alpha mixing
  Config.Blit368W = 1; //0:old, 1:new1, 2:new2(linear)

  //zear - Added option to store the last visited directory.
  strncpy(Config.LastDir, homedir, MAXPATHLEN); /* Defaults to home directory. */
  Config.LastDir[MAXPATHLEN-1] = '\0';

  // senquack - added spu_pcsxrearmed plugin:
#ifdef SPU_PCSXREARMED
  //ORIGINAL PCSX ReARMed SPU defaults (put here for reference):
  //	spu_config.iUseReverb = 1;
  //	spu_config.iUseInterpolation = 1;
  //	spu_config.iXAPitch = 0;
  //	spu_config.iVolume = 768;
  //	spu_config.iTempo = 0;
  //	spu_config.iUseThread = 1; // no effect if only 1 core is detected
  //	// LOW-END DEVICE:
  //	#ifdef HAVE_PRE_ARMV7 /* XXX GPH hack */
  //		spu_config.iUseReverb = 0;
  //		spu_config.iUseInterpolation = 0;
  //		spu_config.iTempo = 1;
  //	#endif

  // PCSX4ALL defaults:
  // NOTE: iUseThread *will* have an effect even on a single-core device, but
  //		 results have yet to be tested. TODO: test if using iUseThread can
  //		 improve sound dropouts in any cases.
  spu_config.iHaveConfiguration = 1;    // *MUST* be set to 1 before calling SPU_Init()
  spu_config.iUseReverb = 0;
  spu_config.iUseInterpolation = 0;
  spu_config.iXAPitch = 0;
  spu_config.iVolume = 1024;            // 1024 is max volume
  spu_config.iUseThread = 0;            // no effect if only 1 core is detected
  spu_config.iUseFixedUpdates = 1;      // This is always set to 1 in libretro's pcsxReARMed
  spu_config.iTempo = 1;                // see note below
#endif

  //senquack - NOTE REGARDING iTempo config var above
  // From thread https://pyra-handheld.com/boards/threads/pcsx-rearmed-r22-now-using-the-dsp.75388/
  // Notaz says that setting iTempo=1 restores pcsxreARMed SPU's old behavior, which allows slow emulation
  // to not introduce audio dropouts (at least I *think* he's referring to iTempo config setting)
  // "Probably the main change is SPU emulation, there were issues in some games where effects were wrong,
  //  mostly Final Fantasy series, it should be better now. There were also sound sync issues where game would
  //  occasionally lock up (like Valkyrie Profile), it should be stable now.
  //  Changed sync has a side effect however - if the emulator is not fast enough (may happen with double
  //  resolution mode or while underclocking), sound will stutter more instead of slowing down the music itself.
  //  There is a new option in SPU plugin config to restore old inaccurate behavior if anyone wants it." -Notaz

  // gpu_dfxvideo
#ifdef GPU_DFXVIDEO
  extern int UseFrameLimit;
  UseFrameLimit=0; // limit fps 1=on, 0=off
  extern int UseFrameSkip;
  UseFrameSkip=0; // frame skip 1=on, 0=off
  extern int iFrameLimit;
  iFrameLimit=0; // fps limit 2=auto 1=fFrameRate, 0=off
  //senquack - TODO: is this really wise to have set to 200 as default:
  extern float fFrameRate;
  fFrameRate=200.0f; // fps
  extern int iUseDither;
  iUseDither=0; // 0=off, 1=game dependant, 2=always
  extern int iUseFixes;
  iUseFixes=0; // use game fixes
  extern uint32_t dwCfgFixes;
  dwCfgFixes=0; // game fixes
  /*
   1=odd/even hack (Chrono Cross)
   2=expand screen width (Capcom fighting games)
   4=ignore brightness color (black screens in Lunar)
   8=disable coordinate check (compatibility mode)
   16=disable cpu saving (for precise framerate)
   32=PC fps calculation (better fps limit in some games)
   64=lazy screen update (Pandemonium 2)
   128=old frame skipping (skip every second frame)
   256=repeated flat tex triangles (Dark Forces)
   512=draw quads with triangles (better g-colors, worse textures)
  */
#endif //GPU_DFXVIDEO

  // gpu_drhell
#ifdef GPU_DRHELL
  extern unsigned int autoFrameSkip;
  autoFrameSkip=1; /* auto frameskip */
  extern signed int framesToSkip;
  framesToSkip=0; /* frames to skip */
#endif //GPU_DRHELL

  // gpu_unai
#ifdef GPU_UNAI
  gpu_unai_config_ext.ilace_force = 0;
  gpu_unai_config_ext.clip_368 = 0;
  gpu_unai_config_ext.lighting = 1;
  gpu_unai_config_ext.fast_lighting = 1;
  gpu_unai_config_ext.blending = 1;
  gpu_unai_config_ext.dithering = 0;
#endif

  // Load config from file.
  config_load();
  if (Config.Analog_Mode < 0 || Config.Analog_Mode > 2) Config.Analog_Mode = 0;

  // Check if LastDir exists.
  probe_lastdir();

  // command line options
  uint8_t param_parse_error = 0;
  for (int i = 1; i < argc; i++)
  {
    // PCSX
    // XA audio disabled
    if (strcmp(argv[i],"-noxa") == 0)
      Config.Xa = 1;

    // Black & White MDEC
    if (strcmp(argv[i],"-bwmdec") == 0)
      Config.Mdec = 1;

    // Force PAL system
    if (strcmp(argv[i],"-pal") == 0)
    {
      Config.PsxAuto = 0;
      Config.PsxType = 1;
    }

    // Force NTSC system
    if (strcmp(argv[i],"-ntsc") == 0)
    {
      Config.PsxAuto = 0;
      Config.PsxType = 0;
    }

    // CD audio disabled
    if (strcmp(argv[i],"-nocdda") == 0)
      Config.Cdda = 1;

    // BIOS enabled
    if (strcmp(argv[i],"-bios") == 0)
      Config.HLE = 0;

    // Interpreter enabled
    if (strcmp(argv[i],"-interpreter") == 0)
      Config.Cpu = 1;

    // Parasite Eve 2, Vandal Hearts 1/2 Fix
    if (strcmp(argv[i],"-rcntfix") == 0)
      Config.RCntFix = 1;

    // InuYasha Sengoku Battle Fix
    if (strcmp(argv[i],"-vsyncwa") == 0)
      Config.VSyncWA = 1;

    // SPU IRQ always enabled (fixes audio in some games)
    if (strcmp(argv[i],"-spuirq") == 0)
      Config.SpuIrq = 1;

    // Set ISO file
    if (strcmp(argv[i],"-iso") == 0)
      SetIsoFile(argv[i + 1]);

    // Set executable file
    if (strcmp(argv[i],"-file") == 0)
      strcpy(filename, argv[i + 1]);

    // Audio synchronization option: if audio buffer full, main thread
    //  blocks. Otherwise, just drop the samples.
    if (strcmp(argv[i],"-syncaudio") == 0)
      Config.SyncAudio = 0;

    // Number of times per frame to update SPU. PCSX Rearmed default is once
    //  per frame, but we are more flexible. Valid value is 0..5, where
    //  0 is once per frame, 5 is 32 times per frame (2^5)
    if (strcmp(argv[i],"-spuupdatefreq") == 0)
    {
      int val = -1;
      if (++i < argc)
      {
        val = atoi(argv[i]);
        if (val >= SPU_UPDATE_FREQ_MIN && val <= SPU_UPDATE_FREQ_MAX)
        {
          Config.SpuUpdateFreq = val;
        }
        else val = -1;
      }
      else
      {
        printf("ERROR: missing value for -spuupdatefreq\n");
      }

      if (val == -1)
      {
        printf("ERROR: -spuupdatefreq value must be between %d..%d\n"
               "(%d is once per frame)\n",
               SPU_UPDATE_FREQ_MIN, SPU_UPDATE_FREQ_MAX, SPU_UPDATE_FREQ_1);
        param_parse_error = 1;
        break;
      }
    }

    //senquack - Added option to allow queuing CDREAD_INT interrupts sooner
    //           than they'd normally be issued when SPU's XA buffer is not
    //           full. This fixes droupouts in music/speech on slow devices.
    if (strcmp(argv[i],"-forcedxaupdates") == 0)
    {
      int val = -1;
      if (++i < argc)
      {
        val = atoi(argv[i]);
        if (val >= FORCED_XA_UPDATES_MIN && val <= FORCED_XA_UPDATES_MAX)
        {
          Config.ForcedXAUpdates = val;
        }
        else val = -1;
      }
      else
      {
        printf("ERROR: missing value for -forcedxaupdates\n");
      }

      if (val == -1)
      {
        printf("ERROR: -forcedxaupdates value must be between %d..%d\n",
               FORCED_XA_UPDATES_MIN, FORCED_XA_UPDATES_MAX);
        param_parse_error = 1;
        break;
      }
    }

    // Performance monitoring options
    if (strcmp(argv[i],"-perfmon") == 0)
    {
      // Enable detailed stats and console output
      Config.PerfmonConsoleOutput = 1;
      Config.PerfmonDetailedStats = 1;
    }

    // GPU
    // show FPS
    if (strcmp(argv[i],"-showfps") == 0)
    {
      Config.ShowFps = 1;
    }

    // frame limit
    if (strcmp(argv[i],"-noframelimit") == 0)
    {
      Config.FrameLimit = 0;
    }

    // frame skip
    if (strcmp(argv[i],"-frameskip") == 0)
    {
      int val = -1000;
      if (++i < argc)
      {
        val = atoi(argv[i]);
        if (val >= -1 && val <= 3)
        {
          Config.FrameSkip = val;
        }
      }
      else
      {
        printf("ERROR: missing value for -frameskip\n");
      }

      if (val == -1000)
      {
        printf("ERROR: -frameskip value must be between -1..3 (-1 is AUTO)\n");
        param_parse_error = 1;
        break;
      }
    }

#ifdef GPU_UNAI
    // Render only every other line (looks ugly but faster)
    if (strcmp(argv[i],"-interlace") == 0)
    {
      gpu_unai_config_ext.ilace_force = 1;
    }

    // Allow 24bpp->15bpp dithering (only polys, only if PS1 game uses it)
    if (strcmp(argv[i],"-dither") == 0)
    {
      gpu_unai_config_ext.dithering = 1;
    }

    if (strcmp(argv[i],"-nolight") == 0)
    {
      gpu_unai_config_ext.lighting = 0;
    }

    if (strcmp(argv[i],"-noblend") == 0)
    {
      gpu_unai_config_ext.blending = 0;
    }

    // Apply lighting to all primitives. Default is to only light primitives
    //  with light values below a certain threshold (for speed).
    if (strcmp(argv[i],"-nofastlight") == 0)
    {
      gpu_unai_config_ext.fast_lighting = 0;
    }

    // Render all pixels on a horizontal line, even when in hi-res 512,640
    //  PSX vid modes and those pixels would never appear on 320x240 screen.
    //  (when using pixel-dropping downscaler).
    //  Can cause visual artifacts, default is on for now (for speed)
    if (strcmp(argv[i],"-nopixelskip") == 0)
    {
      gpu_unai_config_ext.clip_368 = 0;
      gpu_unai_config_ext.pixel_skip = 0;
    }

    // Settings specific to older, non-gpulib standalone gpu_unai:
#ifndef USE_GPULIB
    // Progressive interlace option - See gpu_unai/gpu.h
    // Old option left in from when PCSX4ALL ran on very slow devices.
    if (strcmp(argv[i],"-progressive") == 0)
    {
      gpu_unai_config_ext.prog_ilace = 1;
    }
#endif //!USE_GPULIB
#endif //GPU_UNAI


    // SPU
#ifndef SPU_NULL

    // ----- BEGIN SPU_PCSXREARMED SECTION -----
#ifdef SPU_PCSXREARMED
    // No sound
    if (strcmp(argv[i],"-silent") == 0)
    {
      spu_config.iDisabled = 1;
    }
    // Reverb
    if (strcmp(argv[i],"-reverb") == 0)
    {
      spu_config.iUseReverb = 1;
    }
    // XA Pitch change support
    if (strcmp(argv[i],"-xapitch") == 0)
    {
      spu_config.iXAPitch = 1;
    }

    // Enable SPU thread
    // NOTE: By default, PCSX ReARMed would not launch
    //  a thread if only one core was detected, but I have
    //  changed it to allow it under any case.
    // TODO: test if any benefit is ever achieved
    if (strcmp(argv[i],"-threaded_spu") == 0)
    {
      spu_config.iUseThread = 1;
    }

    // Don't output fixed number of samples per frame
    // (unknown if this helps or hurts performance
    //  or compatibility.) The default in all builds
    //  of PCSX_ReARMed is "1", so that is also the
    //  default here.
    if (strcmp(argv[i],"-nofixedupdates") == 0)
    {
      spu_config.iUseFixedUpdates = 0;
    }

    // Set interpolation none/simple/gaussian/cubic, default is none
    if (strcmp(argv[i],"-interpolation") == 0)
    {
      int val = -1;
      if (++i < argc)
      {
        if (strcmp(argv[i],"none") == 0) val=0;
        if (strcmp(argv[i],"simple") == 0) val=1;
        if (strcmp(argv[i],"gaussian") == 0) val=2;
        if (strcmp(argv[i],"cubic") == 0) val=3;
      }
      else
        printf("ERROR: missing value for -interpolation\n");


      if (val == -1)
      {
        printf("ERROR: -interpolation value must be one of: none,simple,gaussian,cubic\n");
        param_parse_error = 1;
        break;
      }

      spu_config.iUseInterpolation = val;
    }

    // Set volume level of SPU, 0-1024
    //  If value is 0, sound will be disabled.
    if (strcmp(argv[i],"-volume") == 0)
    {
      int val = -1;
      if (++i < argc)
        val = atoi(argv[i]);
      else
        printf("ERROR: missing value for -volume\n");

      if (val < 0 || val > 1024)
      {
        printf("ERROR: -volume value must be between 0-1024. Value of 0 will mute sound\n"
               "        but SPU plugin will still run, ensuring best compatibility.\n"
               "        Use -silent flag to disable SPU plugin entirely.\n");
        param_parse_error = 1;
        break;
      }

      spu_config.iVolume = val;
    }

    // SPU will issue updates at a rate that ensures better
    //  compatibility, but if the emulator runs too slowly,
    //  audio stutter will be increased. "False" is the
    //  default setting on Pandora/Pyra/Android builds of
    //  PCSX_ReARMed, but Wiz/Caanoo builds used the faster
    //  inaccurate setting, "1", so I've made our default
    //  "1" as well, since we target low-end devices.
    if (strcmp(argv[i],"-notempo") == 0)
    {
      spu_config.iTempo = 0;
    }

    //NOTE REGARDING ABOVE SETTING "spu_config.iTempo":
    // From thread https://pyra-handheld.com/boards/threads/pcsx-rearmed-r22-now-using-the-dsp.75388/
    // Notaz says that setting iTempo=1 restores pcsxreARMed SPU's old behavior, which allows slow emulation
    // to not introduce audio dropouts (at least I *think* he's referring to iTempo config setting)
    // "Probably the main change is SPU emulation, there were issues in some games where effects were wrong,
    //  mostly Final Fantasy series, it should be better now. There were also sound sync issues where game would
    //  occasionally lock up (like Valkyrie Profile), it should be stable now.
    //  Changed sync has a side effect however - if the emulator is not fast enough (may happen with double
    //  resolution mode or while underclocking), sound will stutter more instead of slowing down the music itself.
    //  There is a new option in SPU plugin config to restore old inaccurate behavior if anyone wants it." -Notaz

#endif //SPU_PCSXREARMED
    // ----- END SPU_PCSXREARMED SECTION -----

#endif //!SPU_NULL
  }

  if (param_parse_error)
  {
    printf("Failed to parse command-line parameters, exiting.\n");
    exit(1);
  }

  //NOTE: spu_pcsxrearmed will handle audio initialization
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);

  atexit(pcsx4all_exit);

#ifdef SDL_TRIPLEBUF
  int flags = SDL_HWSURFACE | SDL_TRIPLEBUF;
#else
  int flags = SDL_HWSURFACE | SDL_DOUBLEBUF;
#endif
	
  screen = SDL_SetVideoMode(320, 240, 16, flags);
  if (!screen)
  {
    puts("Failed to set video mode");
    exit(0);
  }

  if (SDL_MUSTLOCK(screen))
    SDL_LockSurface(screen);

  SDL_WM_SetCaption("pcsx4all - SDL Version", "pcsx4all");

  SCREEN = (Uint16 *)screen->pixels;

  if (argc < 2 || cdrfilename[0] == '\0')
  {
    // Enter frontend main-menu:
    emu_running = 0;
    if (!SelectGame())
    {
      printf("ERROR: missing filename for -iso\n");
      exit(1);
    }
  }

  if (psxInit() == -1)
  {
    printf("PSX emulator couldn't be initialized.\n");
    exit(1);
  }

  if (LoadPlugins() == -1)
  {
    printf("Failed loading plugins.\n");
    exit(1);
  }

  pcsx4all_initted = 1;
  emu_running = 1;

  // Initialize plugin_lib, gpulib
  pl_init();

  if (cdrfilename[0] != '\0')
  {
    if (CheckCdrom() == -1)
    {
      psxReset();
      printf("Failed checking ISO image.\n");
      SetIsoFile(NULL);
    }
    else
    {
      check_spec_bios();
      psxReset();
      printf("Running ISO image: %s.\n", cdrfilename);
      if (LoadCdrom() == -1)
      {
        printf("Failed loading ISO image.\n");
        SetIsoFile(NULL);
      }
    }
  }
  else
  {
    psxReset();
  }

  if (filename[0] != '\0')
  {
    if (Load(filename) == -1)
    {
      printf("Failed loading executable.\n");
      filename[0]='\0';
    }
  }

  CheckforCDROMid_applyhacks();
  
  joy_init();
  
  Rumble_Init();

  if (filename[0] != '\0')
  {
    printf("Running executable: %s.\n",filename);
  }

  if ((cdrfilename[0] == '\0') && (filename[0] == '\0') && (Config.HLE == 0))
  {
    printf("Running BIOS.\n");
  }

  if ((cdrfilename[0] != '\0') || (filename[0] != '\0') || (Config.HLE == 0))
  {
    psxCpu->Execute();
  }

  return 0;
}

unsigned get_ticks(void)
{
#ifdef TIME_IN_MSEC
  return SDL_GetTicks();
#else
  return ((((unsigned long long)clock())*1000000ULL)/((unsigned long long)CLOCKS_PER_SEC));
#endif
}

void wait_ticks(unsigned s)
{
#ifdef TIME_IN_MSEC
  SDL_Delay(s);
#else
  SDL_Delay(s/1000);
#endif
}

void port_printf(int x, int y, const char *text)
{
  static const unsigned char fontdata8x8[] =
  {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x3C,0x42,0x99,0xBD,0xBD,0x99,0x42,0x3C,0x3C,0x42,0x81,0x81,0x81,0x81,0x42,0x3C,
    0xFE,0x82,0x8A,0xD2,0xA2,0x82,0xFE,0x00,0xFE,0x82,0x82,0x82,0x82,0x82,0xFE,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x64,0x74,0x7C,0x38,0x00,0x00,
    0x80,0xC0,0xF0,0xFC,0xF0,0xC0,0x80,0x00,0x01,0x03,0x0F,0x3F,0x0F,0x03,0x01,0x00,
    0x18,0x3C,0x7E,0x18,0x7E,0x3C,0x18,0x00,0xEE,0xEE,0xEE,0xCC,0x00,0xCC,0xCC,0x00,
    0x00,0x00,0x30,0x68,0x78,0x30,0x00,0x00,0x00,0x38,0x64,0x74,0x7C,0x38,0x00,0x00,
    0x3C,0x66,0x7A,0x7A,0x7E,0x7E,0x3C,0x00,0x0E,0x3E,0x3A,0x22,0x26,0x6E,0xE4,0x40,
    0x18,0x3C,0x7E,0x3C,0x3C,0x3C,0x3C,0x00,0x3C,0x3C,0x3C,0x3C,0x7E,0x3C,0x18,0x00,
    0x08,0x7C,0x7E,0x7E,0x7C,0x08,0x00,0x00,0x10,0x3E,0x7E,0x7E,0x3E,0x10,0x00,0x00,
    0x58,0x2A,0xDC,0xC8,0xDC,0x2A,0x58,0x00,0x24,0x66,0xFF,0xFF,0x66,0x24,0x00,0x00,
    0x00,0x10,0x10,0x38,0x38,0x7C,0xFE,0x00,0xFE,0x7C,0x38,0x38,0x10,0x10,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1C,0x1C,0x1C,0x18,0x00,0x18,0x18,0x00,
    0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x7C,0x28,0x7C,0x28,0x00,0x00,
    0x10,0x38,0x60,0x38,0x0C,0x78,0x10,0x00,0x40,0xA4,0x48,0x10,0x24,0x4A,0x04,0x00,
    0x18,0x34,0x18,0x3A,0x6C,0x66,0x3A,0x00,0x18,0x18,0x20,0x00,0x00,0x00,0x00,0x00,
    0x30,0x60,0x60,0x60,0x60,0x60,0x30,0x00,0x0C,0x06,0x06,0x06,0x06,0x06,0x0C,0x00,
    0x10,0x54,0x38,0x7C,0x38,0x54,0x10,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,
    0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x3E,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x04,0x08,0x10,0x20,0x40,0x00,0x00,
    0x38,0x4C,0xC6,0xC6,0xC6,0x64,0x38,0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00,
    0x7C,0xC6,0x0E,0x3C,0x78,0xE0,0xFE,0x00,0x7E,0x0C,0x18,0x3C,0x06,0xC6,0x7C,0x00,
    0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00,0xFC,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00,
    0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00,0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00,
    0x78,0xC4,0xE4,0x78,0x86,0x86,0x7C,0x00,0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00,
    0x00,0x00,0x18,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x18,0x18,0x30,
    0x1C,0x38,0x70,0xE0,0x70,0x38,0x1C,0x00,0x00,0x7C,0x00,0x00,0x7C,0x00,0x00,0x00,
    0x70,0x38,0x1C,0x0E,0x1C,0x38,0x70,0x00,0x7C,0xC6,0xC6,0x1C,0x18,0x00,0x18,0x00,
    0x3C,0x42,0x99,0xA1,0xA5,0x99,0x42,0x3C,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00,
    0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00,0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00,
    0xF8,0xCC,0xC6,0xC6,0xC6,0xCC,0xF8,0x00,0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xFE,0x00,
    0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0x00,0x3E,0x60,0xC0,0xCE,0xC6,0x66,0x3E,0x00,
    0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,
    0x06,0x06,0x06,0x06,0xC6,0xC6,0x7C,0x00,0xC6,0xCC,0xD8,0xF0,0xF8,0xDC,0xCE,0x00,
    0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00,
    0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,
    0xFC,0xC6,0xC6,0xC6,0xFC,0xC0,0xC0,0x00,0x7C,0xC6,0xC6,0xC6,0xDE,0xCC,0x7A,0x00,
    0xFC,0xC6,0xC6,0xCE,0xF8,0xDC,0xCE,0x00,0x78,0xCC,0xC0,0x7C,0x06,0xC6,0x7C,0x00,
    0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,
    0xC6,0xC6,0xC6,0xEE,0x7C,0x38,0x10,0x00,0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00,
    0xC6,0xEE,0x3C,0x38,0x7C,0xEE,0xC6,0x00,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00,
    0xFE,0x0E,0x1C,0x38,0x70,0xE0,0xFE,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,
    0x60,0x60,0x30,0x18,0x0C,0x06,0x06,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,
    0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
    0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x66,0x3C,0x00,
    0x60,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x3C,0x66,0x60,0x60,0x66,0x3C,0x00,
    0x06,0x3E,0x66,0x66,0x66,0x66,0x3E,0x00,0x00,0x3C,0x66,0x66,0x7E,0x60,0x3C,0x00,
    0x1C,0x30,0x78,0x30,0x30,0x30,0x30,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x3C,
    0x60,0x7C,0x76,0x66,0x66,0x66,0x66,0x00,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x00,
    0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0x38,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00,
    0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0xEC,0xFE,0xFE,0xFE,0xD6,0xC6,0x00,
    0x00,0x7C,0x76,0x66,0x66,0x66,0x66,0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00,
    0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x06,
    0x00,0x7E,0x70,0x60,0x60,0x60,0x60,0x00,0x00,0x3C,0x60,0x3C,0x06,0x66,0x3C,0x00,
    0x30,0x78,0x30,0x30,0x30,0x30,0x1C,0x00,0x00,0x66,0x66,0x66,0x66,0x6E,0x3E,0x00,
    0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0xC6,0xD6,0xFE,0xFE,0x7C,0x6C,0x00,
    0x00,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x06,0x3C,
    0x00,0x7E,0x0C,0x18,0x30,0x60,0x7E,0x00,0x0E,0x18,0x0C,0x38,0x0C,0x18,0x0E,0x00,
    0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00,0x70,0x18,0x30,0x1C,0x30,0x18,0x70,0x00,
    0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x10,0x28,0x10,0x54,0xAA,0x44,0x00,0x00,
  };

  int interval = 1, row = 320 * interval;
  unsigned short *screen = (SCREEN + x + y * 320);
  int len = strlen(text);

  for (int i = 0; i < len; i++)
  {
    int pos = 0;
    for (int l = 0; l < 8; l++)
    {
      unsigned char data = fontdata8x8[((text[i]) * 8) + l];
      screen[pos    ] = (data & 0x80) ? 0xffff:0x0000;
      screen[pos + 1] = (data & 0x40) ? 0xffff:0x0000;
      screen[pos + 2] = (data & 0x20) ? 0xffff:0x0000;
      screen[pos + 3] = (data & 0x10) ? 0xffff:0x0000;
      screen[pos + 4] = (data & 0x08) ? 0xffff:0x0000;
      screen[pos + 5] = (data & 0x04) ? 0xffff:0x0000;
      screen[pos + 6] = (data & 0x02) ? 0xffff:0x0000;
      screen[pos + 7] = (data & 0x01) ? 0xffff:0x0000;
      pos += row;
    }
    screen += 8;
  }
}


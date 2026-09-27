/* Compiles every module interface header together (phase 5 step 1), so a conflicting or broken
 * declaration fails the build even before the module that owns it has any code. No definitions. */
#include "platform/platform.h"
#include "platform/rtl.h"
#include "video/video.h"
#include "sound/sound.h"
#include "shell_ui/shell_ui.h"
#include "shell_data/shell_data.h"
#include "sim_flow/sim_flow.h"
#include "sim_world/sim_world.h"
#include "render3d/render3d.h"
#include "game/game.h"

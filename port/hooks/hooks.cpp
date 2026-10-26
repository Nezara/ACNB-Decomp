/* Port hooks (acnb/hooks.h): defaults = the original game's behaviour. */
#include "acnb/hooks.h"

void (*hook_sortie_logic_frame)(void) = nullptr;
void (*hook_mpart_asm_default)(void) = nullptr;
void (*hook_distribute_save)(void) = nullptr;
int (*hook_mobj_string_layout_id)(int id) = nullptr;
int (*hook_mobj_skip_string_color_all)(void) = nullptr;
int (*hook_unitsprite_skip_multiply)(void) = nullptr;
void (*hook_init_mem_card)(void) = nullptr;
int (*hook_garage_item_mode)(int item, int mode) = nullptr;
void (*hook_garage_top_create_item)(int enter) = nullptr;
void (*hook_garage_new_sub_sys_def)(unsigned int *helpStrId) = nullptr;
struct stMobj *(*hook_mmanage_new_system)(void) = nullptr;
void (*hook_step_map_select)(void) = nullptr;
int (*hook_field_store)(int field) = nullptr;
void (*hook_view_screen)(int cam, float *ax, float *scrz) = nullptr;
void (*hook_cull_frustum)(float *fx, float *scrz) = nullptr;
const struct AcnbIlinkOps *hook_ilink = nullptr;
void (*hook_sortie_frame_end)(unsigned int frame, const void *ac, const void *rand_next) = nullptr;

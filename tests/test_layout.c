/* Unit tests for layout rescaling on a slot_size change.
 *
 * These exercise the pure layout math (no X display): layout.c, icons.c and the
 * rest of core_sources are linked, settings/tray_data are set up by hand, and
 * the functions under test only read/write those globals plus the icon list.
 *
 * Regression focus: a slot_size reload must keep every icon in the grid slot it
 * already occupies. layout_rescale() must NOT re-derive positions from list
 * order, which is not guaranteed to mirror grid order (an icon shown via the
 * hidden->visible path is placed by best fit, leaving its list position and
 * grid slot out of sync -- this reordered the tray on the first SIGHUP). */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h> /* cmocka requires this before <cmocka.h> */

#include <cmocka.h>

#include "../src/icons.h"
#include "../src/layout.h"
#include "../src/settings.h"
#include "../src/tray.h"

/* A fresh, display-free environment per test: default settings, a zeroed
 * tray_data (so scrollbar offsets are zero), a horizontal west-gravity tray
 * with a known slot pitch and generous max dimensions. */
static int setup(void **state)
{
  (void)state;
  init_default_settings();
  memset(&tray_data, 0, sizeof(tray_data));
  settings.vertical = 0;
  settings.icon_gravity = GRAV_N | GRAV_W;
  settings.scrollbars_mode = SB_MODE_NONE;
  settings.slot_size.x = 20;
  settings.slot_size.y = 16;
  settings.max_tray_dims.x = 1000;
  settings.max_tray_dims.y = 1000;
  return 0;
}

static int teardown(void **state)
{
  (void)state;
  while (icons_head != NULL) icon_list_free(icons_head);
  free_settings(&settings);
  return 0;
}

/* Append a laid-out, visible icon at a given grid slot. icon_list_new prepends,
 * so the first-created icon ends up at the list tail. */
static struct TrayIcon *add_icon_at(Window wid, int grid_x)
{
  struct TrayIcon *ti = icon_list_new(wid, 0);
  ti->is_visible = True;
  ti->is_layed_out = True;
  ti->l.wnd_sz.x = 16;
  ti->l.wnd_sz.y = 16;
  ti->l.grd_rect.x = grid_x;
  ti->l.grd_rect.y = 0;
  ti->l.grd_rect.w = 1;
  ti->l.grd_rect.h = 1;
  return ti;
}

/* The core regression: list order diverges from grid order (the list-head icon
 * sits at grid slot 2), and layout_rescale must leave every grid slot exactly
 * where it was -- only the pixel translation changes. */
static void test_rescale_preserves_grid_slots(void **state)
{
  (void)state;
  /* Build list [head, mid, tail] = [slot 2, slot 0, slot 1] by creating the
   * tail first. This mirrors the bug: a best-fit-placed icon (slot 2) lands at
   * the list head while the rest stay in grid order. */
  struct TrayIcon *g1 = add_icon_at(0x1, 1); /* list tail */
  struct TrayIcon *g0 = add_icon_at(0x0, 0); /* list middle */
  struct TrayIcon *hi = add_icon_at(0x2, 2); /* list head, high slot */
  int w, h;

  assert_ptr_equal(icons_head, hi);

  layout_rescale();

  /* Grid slots are untouched (no reordering by list position). */
  assert_int_equal(hi->l.grd_rect.x, 2);
  assert_int_equal(g0->l.grd_rect.x, 0);
  assert_int_equal(g1->l.grd_rect.x, 1);

  /* West gravity, no scrollbars: pixel x = slot * slot_size.x. */
  assert_int_equal(hi->l.icn_rect.x, 40);
  assert_int_equal(g0->l.icn_rect.x, 0);
  assert_int_equal(g1->l.icn_rect.x, 20);

  /* Grid spans three slots of 20px. */
  layout_get_size(&w, &h);
  assert_int_equal(w, 60);
  assert_int_equal(h, 16);
}

/* A new slot pitch re-translates pixels but keeps slots (and grid extent in
 * slot units) the same. */
static void test_rescale_applies_new_slot_size(void **state)
{
  (void)state;
  add_icon_at(0x1, 1);
  struct TrayIcon *g0 = add_icon_at(0x0, 0);
  struct TrayIcon *hi = add_icon_at(0x2, 2);
  int w, h;

  settings.slot_size.x = 30;
  layout_rescale();

  assert_int_equal(hi->l.grd_rect.x, 2);
  assert_int_equal(g0->l.icn_rect.x, 0);
  assert_int_equal(hi->l.icn_rect.x, 60);

  layout_get_size(&w, &h);
  assert_int_equal(w, 90); /* 3 slots * 30px */
}

/* Per-cell spans depend on the slot size: an icon wider than the slot occupies
 * more than one cell, and that recomputes when the pitch changes. */
static void test_rescale_recomputes_cell_spans(void **state)
{
  (void)state;
  struct TrayIcon *big = add_icon_at(0x9, 0);
  big->l.wnd_sz.x = 40; /* two 20px cells wide */

  layout_rescale();
  assert_int_equal(big->l.grd_rect.w, 2);

  settings.slot_size.x = 40; /* now fits in a single cell */
  layout_rescale();
  assert_int_equal(big->l.grd_rect.w, 1);
}

/* Contrast/guard: layout_relayout_in_list_order deliberately assigns slots from
 * list order, so it WOULD move the list-head icon to slot 0. This is exactly
 * why the slot_size reload path must not use it. */
static void test_relayout_in_list_order_follows_list(void **state)
{
  (void)state;
  add_icon_at(0x1, 1);
  add_icon_at(0x0, 0);
  struct TrayIcon *hi = add_icon_at(0x2, 2); /* list head */

  layout_relayout_in_list_order();

  /* The head icon is reassigned to the first slot -- the reordering bug. */
  assert_int_equal(hi->l.grd_rect.x, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(
          test_rescale_preserves_grid_slots, setup, teardown),
      cmocka_unit_test_setup_teardown(
          test_rescale_applies_new_slot_size, setup, teardown),
      cmocka_unit_test_setup_teardown(
          test_rescale_recomputes_cell_spans, setup, teardown),
      cmocka_unit_test_setup_teardown(
          test_relayout_in_list_order_follows_list, setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}

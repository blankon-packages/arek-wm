/* arek-wm.c
 *
 * Copyright (C) 2015 Jente Hidskes <hjdskes@gmail.com>
 *
 * Window mapping and destroying animations are copied from Budgie WM by Ikey Doherty.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <meta/display.h>
#include <meta/meta-plugin.h>

#include "arek-wm.h"
#include "background.h"
#include "keybindings.h"
#include "meta-wrapper.h"
#include "tile.h"
#include "windowlist.h"
#include "workspace.h"

#define AREK_WM_SCHEMA  "com.unia.arek.wm"
#define DESTROY_TIMEOUT 130
#define DESTROY_SCALE   0.6f
#define MAP_TIMEOUT     150
#define MAP_SCALE       0.8f
#define FADE_TIMEOUT    115
#define SHOW_TIMEOUT    1000
#define SWITCH_TIMEOUT  250

static ClutterPoint PV_CENTER = { 0.5f, 0.5f };
static ClutterPoint PV_NORM   = { 0.0f, 0.0f };

G_DEFINE_TYPE (ArekWm, arek_wm, META_TYPE_PLUGIN)

static const MetaPluginInfo *
arek_wm_plugin_info (__attribute__ ((unused)) MetaPlugin *plugin)
{
	static const MetaPluginInfo info = {
		.name = "Arek window manager",
		.version = VERSION,
		.author = "Jente Hidskes",
		.license = "GPLv2",
		.description = "A modern tiling window manager"
	};
	return &info;
}

static void
arek_wm_kill_switch_workspace (MetaPlugin *plugin)
{
	ArekWm *wm;

	wm = AREK_WM (plugin);

	if (wm->out_group) {
		g_signal_emit_by_name (wm->out_group,
				       "transitions-completed",
				       wm->out_group,
				       wm,
				       NULL);
	}
}

static void
arek_wm_kill_window_effects (MetaPlugin *plugin, MetaWindowActor *actor)
{
	g_signal_emit_by_name (actor, "transitions-completed",
			       actor, plugin, NULL);
}

static void
switch_workspace_done (__attribute__ ((unused)) ClutterActor *actor,
		       gpointer user_data)
{
	ArekWm *wm;
	GList *l;

	wm = AREK_WM (user_data);
	l = meta_get_window_actors (wm->screen);

	while (l) {
		ClutterActor *orig_parent = g_object_get_data (G_OBJECT (l->data), "orig-parent");

		if (orig_parent) {
			ClutterActor *actor = CLUTTER_ACTOR (l->data);

			g_object_ref (actor);
			clutter_actor_remove_child (clutter_actor_get_parent (actor), actor);
			clutter_actor_add_child (orig_parent, actor);
			g_object_unref (actor);

			g_object_set_data (G_OBJECT (actor), "orig-parent", NULL);
		}

		l = l->next;
	}

	g_signal_handlers_disconnect_by_func (wm->out_group,
					      G_CALLBACK (switch_workspace_done), user_data);
	clutter_actor_remove_all_transitions (wm->out_group);
	clutter_actor_remove_all_transitions (wm->in_group);
	clutter_actor_destroy (wm->out_group);
	clutter_actor_destroy (wm->in_group);
	wm->out_group = NULL;
	wm->in_group = NULL;
	meta_plugin_switch_workspace_completed (META_PLUGIN (user_data));
}

static void
arek_wm_switch_workspace (MetaPlugin *plugin,
			  gint from,
			  gint to,
			  MetaMotionDirection direction)
{
	ArekWm *wm;
	GList *l;
	ClutterActor *stage;
	int screen_width, screen_height;
	int x_dest = 0, y_dest = 0;

	if (from == to) {
		meta_plugin_switch_workspace_completed (plugin);
		return;
	}

	wm = AREK_WM (plugin);
	wm->out_group = clutter_actor_new ();
	wm->in_group = clutter_actor_new ();
	stage = meta_get_stage_for_screen (wm->screen);
	clutter_actor_add_child (stage, wm->in_group);
	clutter_actor_add_child (stage, wm->out_group);
	clutter_actor_set_child_above_sibling (stage, wm->in_group, NULL);
	meta_screen_get_size (wm->screen, &screen_width, &screen_height);

	// TODO: windows should slide "under" the panel/dock
	// TODO: move over "in-between" workspaces, e.g. 1->3 shows 2
	l = meta_get_window_actors (wm->screen);
	while (l) {
		MetaWindow *window = meta_window_actor_get_meta_window (l->data);
		ClutterActor *actor = CLUTTER_ACTOR (l->data);
		MetaWorkspace *space;
		gint win_space;

		if (!meta_window_showing_on_its_workspace (window) ||
		    meta_window_is_on_all_workspaces (window)) {
			l = l->next;
			continue;
		}

		space = meta_window_get_workspace (window);
		win_space = meta_workspace_index (space);
		if (win_space == to || win_space == from) {
			ClutterActor *orig_parent = clutter_actor_get_parent (actor);
			ClutterActor *new_parent = win_space == to ? wm->in_group : wm->out_group;

			g_object_set_data (G_OBJECT (actor),
					   "orig-parent", orig_parent);

			g_object_ref (actor);
			clutter_actor_remove_child (orig_parent, actor);
			clutter_actor_add_child (new_parent, actor);
			g_object_unref (actor);
		}

		l = l->next;
	}

	if (direction == META_MOTION_UP ||
	    direction == META_MOTION_UP_LEFT ||
	    direction == META_MOTION_UP_RIGHT) {
		y_dest = screen_height;
	} else if (direction == META_MOTION_DOWN ||
		   direction == META_MOTION_DOWN_LEFT ||
		   direction == META_MOTION_DOWN_RIGHT) {
		y_dest = -screen_height;
	}

	if (direction == META_MOTION_LEFT ||
	    direction == META_MOTION_UP_LEFT ||
	    direction == META_MOTION_DOWN_LEFT) {
		x_dest = screen_width;
	} else if (direction == META_MOTION_RIGHT ||
		   direction == META_MOTION_UP_RIGHT ||
		   direction == META_MOTION_DOWN_RIGHT) {
		x_dest = -screen_width;
	}

	/* Animate-in the new workspace. */
	clutter_actor_set_position (wm->in_group, -x_dest, -y_dest);
	clutter_actor_save_easing_state (wm->in_group);
	clutter_actor_set_easing_mode (wm->in_group, CLUTTER_EASE_OUT_QUAD);
	clutter_actor_set_easing_duration (wm->in_group, SWITCH_TIMEOUT);
	clutter_actor_set_position (wm->in_group, 0, 0);
	clutter_actor_restore_easing_state (wm->in_group);

	/* Animate-out the previous workspace. */
	g_signal_connect (wm->out_group, "transitions-completed",
			  G_CALLBACK (switch_workspace_done), plugin);
	clutter_actor_save_easing_state (wm->out_group);
	clutter_actor_set_easing_mode (wm->out_group, CLUTTER_EASE_OUT_QUAD);
	clutter_actor_set_easing_duration (wm->out_group, SWITCH_TIMEOUT);
	clutter_actor_set_position (wm->out_group, x_dest, y_dest);
	clutter_actor_restore_easing_state (wm->out_group);
}

static void
destroy_done (ClutterActor *actor, gpointer user_data)
{
	MetaPlugin *plugin;

	plugin = META_PLUGIN (user_data);

	clutter_actor_remove_all_transitions (actor);
	g_signal_handlers_disconnect_by_func (actor, G_CALLBACK (destroy_done), plugin);
	meta_plugin_destroy_completed (plugin, META_WINDOW_ACTOR (actor));
}

static void
arek_wm_destroy (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
	ArekWm *wm;
	MetaWindow *window;
	ClutterActor *actor;

	wm = AREK_WM (plugin);
	actor = CLUTTER_ACTOR (window_actor);
	window = meta_window_actor_get_meta_window (window_actor);

	clutter_actor_remove_all_transitions (actor);

	switch (meta_window_get_window_type (window)) {
		case META_WINDOW_NOTIFICATION:
		case META_WINDOW_NORMAL:
		case META_WINDOW_DIALOG:
		case META_WINDOW_MODAL_DIALOG:
			/* Initialise animation. */
			g_object_set (actor, "pivot-point", &PV_CENTER, NULL);
			clutter_actor_save_easing_state (actor);
			clutter_actor_set_easing_mode (actor, CLUTTER_EASE_OUT_QUAD);
			clutter_actor_set_easing_duration (actor, DESTROY_TIMEOUT);
			g_signal_connect (actor, "transitions-completed",
					  G_CALLBACK (destroy_done), plugin);

			/* Now animate. */
			g_object_set (actor, "scale-x", DESTROY_SCALE, "scale-y", DESTROY_SCALE, "opacity", 0, NULL);
			clutter_actor_restore_easing_state (actor);

			/* Remove the window from the list of managed windows
			 * and retile if necessary. */
			arek_wm_remove_from_list (wm, window);
			if (!meta_window_is_floating (window) && !meta_window_is_minimized (window)) {
				arek_wm_retile (wm, meta_window_get_workspace (window));
			}

			break;
		default:
			meta_plugin_destroy_completed (plugin, window_actor);
			break;
	}
}

static void
map_done (ClutterActor *actor, gpointer user_data)
{
	MetaPlugin *plugin;

	plugin = META_PLUGIN (user_data);

	clutter_actor_remove_all_transitions (actor);
	g_signal_handlers_disconnect_by_func (actor, G_CALLBACK (map_done), plugin);
	g_object_set (actor, "pivot-point", &PV_NORM, NULL);
	meta_plugin_map_completed (plugin, META_WINDOW_ACTOR (actor));
}

static void
arek_wm_map (MetaPlugin *plugin, MetaWindowActor *window_actor)
{
	ArekWm *wm;
	MetaWindow *window;
	ClutterActor *actor;

	wm = AREK_WM (plugin);
	actor = CLUTTER_ACTOR (window_actor);
	window = meta_window_actor_get_meta_window (window_actor);

	clutter_actor_remove_all_transitions (actor);

	switch (meta_window_get_window_type (window)) {
		case META_WINDOW_POPUP_MENU:
		case META_WINDOW_DROPDOWN_MENU:
		case META_WINDOW_NOTIFICATION:
			/* For menus we'll give em a nice fade-in. */
			g_object_set (actor, "opacity", 0, NULL);
			clutter_actor_show (actor);

			clutter_actor_save_easing_state (actor);
			clutter_actor_set_easing_mode (actor, CLUTTER_EASE_IN_SINE);
			clutter_actor_set_easing_duration (actor, FADE_TIMEOUT);
			g_signal_connect (actor, "transitions-completed",
					  G_CALLBACK (map_done), plugin);

			g_object_set (actor, "opacity", 255, NULL);
			clutter_actor_restore_easing_state (actor);
			break;
		case META_WINDOW_NORMAL:
		case META_WINDOW_DIALOG:
		case META_WINDOW_MODAL_DIALOG:
			// TODO: mapping animation can be smoothened
			g_object_set (actor, "opacity", 0, "scale-x", MAP_SCALE,
				      "scale-y", MAP_SCALE, "pivot-point", &PV_CENTER,
				      NULL);
			clutter_actor_show (actor);

			/* Initialise animation. */
			clutter_actor_save_easing_state (actor);
			clutter_actor_set_easing_mode (actor, CLUTTER_EASE_IN_SINE);
			clutter_actor_set_easing_duration (actor, MAP_TIMEOUT);
			g_signal_connect (actor, "transitions-completed",
					  G_CALLBACK (map_done), plugin);

			/* Now animate. */
			g_object_set (actor, "scale-x", 1.0, "scale-y", 1.0, "opacity", 255, NULL);
			clutter_actor_restore_easing_state (actor);

			/* Windows might remember their state and map maximized
			 * horizontally, vertically of both. This is useless to us,
			 * so we unmaximize them here. */
			// TODO: look at this again when we implement maximizing
			meta_window_unmaximize (window, META_MAXIMIZE_BOTH);

			/* Add the window to the list of managed windows and
			 * tile it or make it float.*/
			arek_wm_add_to_list (wm, window);
			if (arek_wm_can_tile (window)) {
				arek_wm_make_tile (wm, window);
				arek_wm_retile (wm, meta_window_get_workspace (window));
			} else {
				arek_wm_make_float (wm, window);
			}

			break;
		default:
			meta_plugin_map_completed (plugin, window_actor);
			break;
	}
}

static void
on_focus_window_changed (GObject *object,
			 __attribute__ ((unused)) GParamSpec *pspec,
			 gpointer user_data)
{
	ArekWm *wm;
	MetaDisplay *display;
	MetaWindow *window;

	wm = AREK_WM (user_data);
	display = META_DISPLAY (object);

	window = meta_display_get_focus_window (display);
	/* Can return NULL if we aren't managing the window! */
	wm->active_window = g_list_find (wm->windows, window);
}

static void
on_monitors_changed (MetaScreen *screen, gpointer user_data)
{
	ArekWm *wm;
	ClutterActor *background;

	wm = AREK_WM (user_data);
	clutter_actor_destroy_all_children (wm->background_group);

	for (int i = 0; i < meta_screen_get_n_monitors (screen); i++) {
		background = arek_background_new (screen, i);
		clutter_actor_add_child (wm->background_group, background);
		clutter_actor_show (background);
	}
}

static void
on_mfact_step_changed (GSettings *settings, gchar *key, gpointer user_data)
{
	ArekWm *wm;

	wm = AREK_WM (user_data);
	// TODO: when GLib supports this, use floats in GSettings
	wm->mfact_step = (gfloat) g_settings_get_double (settings, key);
}

static void
arek_wm_start (MetaPlugin *plugin)
{
	ArekWm *wm;
	ClutterActor *actors[2];

	wm = AREK_WM (plugin);
	/* Initialise Arek. */
	wm->screen = meta_plugin_get_screen (plugin);
	wm->display = meta_screen_get_display (wm->screen);
	wm->settings = g_settings_new (AREK_WM_SCHEMA);
	g_signal_connect (wm->settings, "changed::mfact-step",
			  G_CALLBACK (on_mfact_step_changed), wm);
	// TODO: when GLib supports this, use floats in GSettings
	wm->mfact_step = (gfloat) g_settings_get_double (wm->settings, "mfact-step");

	/* Setup all existing workspaces. */
	arek_wm_init_workspaces (wm);

	// TODO: manage/tile existing windows

	/* Initialise background. */
	actors[0] = meta_get_window_group_for_screen (wm->screen);
	actors[1] = wm->background_group = meta_background_group_new ();
	clutter_actor_insert_child_below (actors[0], actors[1], NULL);
	g_signal_connect (wm->screen, "monitors-changed",
			  G_CALLBACK (on_monitors_changed), wm);
	on_monitors_changed (wm->screen, wm);

	for (int i = 0; i < 2; i++) {
		clutter_actor_show (actors[i]);
		clutter_actor_set_opacity (actors[i], 0);
	}

	/* Set a static background color before showing the real background. */
	clutter_actor_set_background_color (meta_get_stage_for_screen (wm->screen),
					    CLUTTER_COLOR_DarkRed);
	clutter_actor_show (meta_get_stage_for_screen (wm->screen));

	/* Animate-in background and existing windows. */
	for (int i = 0; i < 2; i++) {
		clutter_actor_save_easing_state (actors[i]);
		clutter_actor_set_easing_mode (actors[i], CLUTTER_EASE_OUT_QUAD);
		clutter_actor_set_easing_duration (actors[i], SHOW_TIMEOUT);
		clutter_actor_set_opacity (actors[i], 255);
		clutter_actor_restore_easing_state (actors[i]);
	}

	/* Always keep a pointer to the currently focused MetaWindow's position
	 * in our list of managed windows. */
	g_signal_connect (wm->display, "notify::focus-window",
			  G_CALLBACK (on_focus_window_changed), wm);

	arek_wm_init_keybindings (wm);
}

static void
arek_wm_dispose (GObject *object)
{
	ArekWm *wm;

	wm = AREK_WM (object);

	if (wm->background_group) {
		clutter_actor_destroy (wm->background_group);
		wm->background_group = NULL;
	}

	if (wm->windows) {
		/* The data in the list is managed by Mutter. */
		g_list_free (wm->windows);
		wm->windows = NULL;
	}

	if (wm->active_window) {
		/* The data in the list is managed by Mutter. */
		g_list_free (wm->active_window);
		wm->active_window = NULL;
	}

	if (wm->settings) {
		g_object_unref (wm->settings);
		wm->settings = NULL;
	}

	if (wm->out_group) {
		clutter_actor_destroy (wm->out_group);
		wm->out_group = NULL;
	}

	if (wm->in_group) {
		clutter_actor_destroy (wm->in_group);
		wm->in_group = NULL;
	}

	G_OBJECT_CLASS (arek_wm_parent_class)->dispose (object);
}

static void
arek_wm_class_init (ArekWmClass *klass)
{
	GObjectClass *g_object_class;
	MetaPluginClass *meta_plugin_class;

	g_object_class = G_OBJECT_CLASS (klass);
	g_object_class->dispose = arek_wm_dispose;

	meta_plugin_class = META_PLUGIN_CLASS (klass);
	meta_plugin_class->start = arek_wm_start;
	//meta_plugin_class->minimize = arek_wm_minimize;
	//meta_plugin_class->unminimize = arek_wm_unminimize;
	//meta_plugin_class->maximize = arek_wm_maximize;
	//meta_plugin_class->unmaximize = arek_wm_unmaximize;
	meta_plugin_class->map = arek_wm_map;
	meta_plugin_class->destroy = arek_wm_destroy;
	meta_plugin_class->switch_workspace = arek_wm_switch_workspace;
	//meta_plugin_class->show_tile_preview = arek_wm_show_tile_preview;
	//meta_plugin_class->hide_tile_preview = arek_wm_hide_tile_preview;
	//meta_plugin_class->show_window_menu = arek_wm_show_window_menu;
	//meta_plugin_class->show_window_menu_for_rect = arek_wm_show_window_menu_for_rect;
	meta_plugin_class->kill_window_effects = arek_wm_kill_window_effects;
	meta_plugin_class->kill_switch_workspace = arek_wm_kill_switch_workspace;
	//meta_plugin_class->xevent_filter = arek_wm_xevent_filter;
	//meta_plugin_class->keybinding_filter = arek_wm_keybinding_filter;
	//meta_plugin_class->confirm_display_change = arek_wm_confirm_display_change;
	meta_plugin_class->plugin_info = arek_wm_plugin_info;
}

static void
arek_wm_init (ArekWm *wm)
{
	wm->screen = NULL;
	wm->display = NULL;
	wm->background_group = NULL;
	wm->windows = NULL;
	wm->active_window = NULL;
	wm->settings = NULL;
	wm->mfact_step = 0.05f;
	wm->out_group = NULL;
	wm->in_group = NULL;

	/* TODO: decide on overriding Mutter behaviour. */
	//meta_prefs_override_preference_schema (const char *key, const char *schema);
}


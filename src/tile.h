/* tile.h
 *
 * Copyright (C) 2015 Jente Hidskes <hjdskes@gmail.com>
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

#ifndef __TILE_H__
#define __TILE_H__

#include <meta/window.h>
#include <meta/workspace.h>

#include "arek-wm.h"

G_BEGIN_DECLS

void arek_wm_retile (ArekWm *wm, MetaWorkspace *space);
gboolean arek_wm_can_tile (MetaWindow *window);
void arek_wm_make_float (ArekWm *wm, MetaWindow *window);
void arek_wm_make_tile (ArekWm *wm, MetaWindow *window);
GList *arek_wm_nexttiled (GList *ws, MetaWorkspace *space);
GList *arek_wm_prevtiled (GList *ws, MetaWorkspace *space);

G_END_DECLS

#endif /* __TILE_H__ */


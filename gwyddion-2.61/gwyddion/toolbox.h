/*
 *  $Id: toolbox.h 23951 2021-08-10 12:18:16Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __GWYDDION_TOOLBOX_H__
#define __GWYDDION_TOOLBOX_H__

#include <gtk/gtkwidget.h>
#include <libgwyddion/gwyenum.h>
#include <libgwymodule/gwymoduleloader.h>

G_BEGIN_DECLS

#define GWY_TOOLBOX_WM_ROLE "gwyddion-toolbox"

typedef enum {
    GWY_APP_ACTION_TYPE_GROUP = -2,    /* Only used in the editor. */
    GWY_APP_ACTION_TYPE_NONE = -1,
    GWY_APP_ACTION_TYPE_PLACEHOLDER = 0,
    GWY_APP_ACTION_TYPE_BUILTIN,
    GWY_APP_ACTION_TYPE_PROC,
    GWY_APP_ACTION_TYPE_GRAPH,
    GWY_APP_ACTION_TYPE_TOOL,
    GWY_APP_ACTION_TYPE_VOLUME,
    GWY_APP_ACTION_TYPE_XYZ,
    GWY_APP_ACTION_TYPE_CMAP,
    GWY_APP_ACTION_NTYPES,
} GwyAppActionType;

typedef struct {
    const gchar *name;
    const gchar *stock_id;
    GCallback callback;
    const gchar *nice_name;  /* Menu path? */
    const gchar *tooltip;
    /* TODO: sens flags? */
} GwyToolboxBuiltinSpec;

/* Representation of the toolbox as given in toolbox.xml.
 * This is something we do not modify, except:
 * (a) in the editor
 * (b) by removal of invalid stuff (during construction).
 * The on-disk file is only written by us when the users uses the editor. */
typedef struct {
    GwyAppActionType type;
    GQuark function;
    GQuark icon;
    GwyRunType mode;
} GwyToolboxItemSpec;

typedef struct {
    GArray *item;
    gchar *name;
    GQuark id;
    gboolean translatable;
} GwyToolboxGroupSpec;

typedef struct {
    GArray *group;
    guint width;
    /* Auxiliary data used only inside parsing. */
    GString *path;
    gboolean seen_tool_placeholder;
} GwyToolboxSpec;

void                         gwy_toolbox_rebuild_to_spec  (GwyToolboxSpec *spec);
GwyToolboxSpec*              gwy_parse_toolbox_ui         (gboolean ignore_user);
gboolean                     gwy_save_toolbox_ui          (GwyToolboxSpec *spec,
                                                           GError **error);
GwyToolboxSpec*              gwy_toolbox_spec_duplicate   (GwyToolboxSpec *spec);
void                         gwy_toolbox_spec_free        (GwyToolboxSpec *spec);
void                         gwy_toolbox_spec_remove_item (GwyToolboxSpec *spec,
                                                           guint i,
                                                           guint j);
void                         gwy_toolbox_spec_remove_group(GwyToolboxSpec *spec,
                                                           guint i);
void                         gwy_toolbox_spec_move_item   (GwyToolboxSpec *spec,
                                                           guint i,
                                                           guint j,
                                                           gboolean up);
void                         gwy_toolbox_spec_move_group  (GwyToolboxSpec *spec,
                                                           guint i,
                                                           gboolean up);
void                         gwy_toolbox_spec_add_item    (GwyToolboxSpec *spec,
                                                           GwyToolboxItemSpec *ispec,
                                                           guint i,
                                                           guint j);
void                         gwy_toolbox_spec_add_group   (GwyToolboxSpec *spec,
                                                           GwyToolboxGroupSpec *gspec,
                                                           guint i);
void                         gwy_toolbox_editor           (void);
const GwyToolboxBuiltinSpec* gwy_toolbox_get_builtins     (guint *nspec);
const GwyToolboxBuiltinSpec* gwy_toolbox_find_builtin_spec(const gchar *name);
const gchar*                 gwy_toolbox_action_type_name (GwyAppActionType type);
GwyAppActionType             gwy_toolbox_find_action_type (const gchar *name);
const gchar*                 gwy_toolbox_mode_name        (GwyRunType mode);
GwyRunType                   gwy_toolbox_find_mode        (const gchar *name);
const gchar*                 gwy_toolbox_action_nice_name (GwyAppActionType type,
                                                           const gchar *name);
const gchar*                 gwy_toolbox_action_stock_id  (GwyAppActionType type,
                                                           const gchar *name);
const gchar*                 gwy_toolbox_action_detail    (GwyAppActionType type,
                                                           const gchar *name);
GwyRunType                   gwy_toolbox_action_run_modes (GwyAppActionType type,
                                                           const gchar *name);

G_END_DECLS

#endif /* __GWYDDION_TOOLBOX_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

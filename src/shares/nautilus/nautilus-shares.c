/* -*- Mode: C; c-file-style: "gnu"; tab-width: 8 -*- */
/* nautilus-gst-shares.c: this file is part of shares-admin, a gnome-system-tool frontend 
 * for shared folders administration.
 * 
 * Copyright (C) 2004 Carlos Garnacho
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: Carlos Garnacho Parro <carlosg@gnome.org>.
 */

#include <config.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <libnautilus-extension/nautilus-extension-types.h>
#include <libnautilus-extension/nautilus-file-info.h>
#include <libnautilus-extension/nautilus-menu-provider.h>
#include <libnautilus-extension/nautilus-info-provider.h>
#include "nautilus-shares.h"

static GType type = 0;

static void nautilus_shares_update (NautilusShares *shares);

static gboolean
is_directory_local (NautilusFileInfo *info)
{
  gchar    *str;
  GFile    *file;
  gboolean  is_local;

  str = nautilus_file_info_get_uri (info);
  file = g_file_new_for_uri (str);

  is_local = g_file_is_native (file);

  g_object_unref (file);
  g_free (str);

  return is_local;
}

static char *
get_path_from_file_info (NautilusFileInfo *info)
{
  GFile *file;
  gchar *str, *path;

  str = nautilus_file_info_get_uri (info);
  file = g_file_new_for_uri (str);

  path = g_file_get_path (file);

  g_object_unref (file);
  g_free (str);

  return path;
}

static void
shares_admin_watch_func (GPid     pid,
			 gint     status,
			 gpointer user_data)
{
  NautilusShares *shares;

  shares = NAUTILUS_SHARES (user_data);
  g_spawn_close_pid (shares->pid);
  shares->pid = 0;

  nautilus_shares_update (shares);
}

static void
on_menu_item_activate (NautilusMenuItem *menu_item,
		       gpointer          data)
{
  NautilusShares *shares;
  NautilusFileInfo *info;
  gchar **args, *dir;
  GError *error = NULL;

  info = g_object_get_data (G_OBJECT (menu_item), "file");
  shares = g_object_get_data (G_OBJECT (menu_item), "shares");
  dir  = get_path_from_file_info (info);

  args = g_new0 (char *, 3);
  args[0] = g_strdup ("shares-admin");
  args[1] = g_strdup_printf ("--add-share=%s", dir);

  g_spawn_async (NULL, args, NULL,
		 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
		 NULL, NULL, &shares->pid, &error);

  if (error)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      shares->pid = 0;
    }
  else
    {
      shares->file_info = g_object_ref (info);
      g_child_watch_add (shares->pid, shares_admin_watch_func, shares);
    }

  g_strfreev (args);
  g_free (dir);
}

static GList*
get_file_items (NautilusMenuProvider *provider,
		GtkWidget            *window,
		GList                *files)
{
  NautilusShares *shares;
  gboolean one_item, is_local, is_dir;
  NautilusFileInfo *info;
  NautilusMenuItem *menu_item;

  shares = NAUTILUS_SHARES (provider);
  one_item = (files && !files->next);

  if (!one_item)
    return NULL;
  
  info = files->data;
  is_dir = nautilus_file_info_is_directory (info);

  if (!is_dir)
    return NULL;

  is_local = is_directory_local (info);

  if (!is_local)
    return NULL;

  menu_item = nautilus_menu_item_new ("NautilusShares::share",
				      _("_Share Folder..."),
				      _("Share this folder with other computers"),
				      "gnome-fs-share");

  /* do not allow running more than one instance from nautilus at the same time */
  g_object_set (menu_item, "sensitive", (shares->pid == 0), NULL);
  g_signal_connect (G_OBJECT (menu_item),
		    "activate",
		    G_CALLBACK (on_menu_item_activate), NULL);

  g_object_set_data (G_OBJECT (menu_item), "file", info);
  g_object_set_data (G_OBJECT (menu_item), "shares", provider);

  return g_list_append (NULL, menu_item);
}

static void
menu_provider_iface_init (NautilusMenuProviderIface *iface)
{
  iface->get_file_items = get_file_items;
}

static gboolean
file_get_share_status_file (NautilusShares   *shares,
			    NautilusFileInfo *file)
{
  char *path;
  gboolean status;

  if (!nautilus_file_info_is_directory(file) || !is_directory_local (file))
    status = FALSE;
  else
    {
      path = get_path_from_file_info (file);
      g_return_val_if_fail (path != NULL, FALSE);

      status = (g_hash_table_lookup (shares->paths, path) != NULL);
      g_free (path);
    }

  return status;
}

static NautilusOperationResult
nautilus_share_update_file_info (NautilusInfoProvider *provider,
                                 NautilusFileInfo *file,
                                 GClosure *update_complete,
                                 NautilusOperationHandle **handle)
{
  NautilusShares *shares;

  shares = NAUTILUS_SHARES (provider);

  if (file_get_share_status_file (shares, file))
    nautilus_file_info_add_emblem (file, "shared");

  return NAUTILUS_OPERATION_COMPLETE;
}

static void
info_provider_iface_init (NautilusInfoProviderIface *iface)
{
  iface->update_file_info = nautilus_share_update_file_info;
}

GType
nautilus_shares_get_type (void)
{
  return type;
}

static void
add_paths (GHashTable *paths,
	   OobsList   *list)
{
  OobsListIter iter;
  gboolean valid;
  GObject *share;
  const gchar *path;

  valid = oobs_list_get_iter_first (list, &iter);

  while (valid)
    {
      share = oobs_list_get (list, &iter);
      path  = oobs_share_get_path (OOBS_SHARE (share));
      valid = oobs_list_iter_next (list, &iter);

      g_hash_table_insert (paths, g_strdup (path), GINT_TO_POINTER (TRUE));
      g_object_unref (share);
    }
}

static void
update_shared_paths (NautilusShares *shares)
{
  /* clean up the paths */
  g_hash_table_remove_all (shares->paths);

  add_paths (shares->paths, oobs_smb_config_get_shares (OOBS_SMB_CONFIG (shares->smb_config)));
  add_paths (shares->paths, oobs_nfs_config_get_shares (OOBS_NFS_CONFIG (shares->nfs_config)));
}

static void
share_object_updated (OobsObject *object,
		      OobsResult  result,
		      gpointer    user_data)
{
  NautilusShares *shares;

  shares = NAUTILUS_SHARES (user_data);
  shares->objects_updating--;

  if (shares->objects_updating > 0)
    return;

  update_shared_paths (shares);

  if (!shares->pid != 0 && shares->file_info)
    {
      /* shares admin has just exited, invalidate file info */
      nautilus_file_info_invalidate_extension_info (shares->file_info);
      g_object_unref (shares->file_info);
      shares->file_info = NULL;
    }
}

static void
nautilus_shares_update_object (NautilusShares *shares,
			       OobsObject     *object)
{
  shares->objects_updating++;
  oobs_object_update_async (object, share_object_updated, shares);
}

static void
nautilus_shares_update (NautilusShares *shares)
{
  nautilus_shares_update_object (shares, shares->smb_config);
  nautilus_shares_update_object (shares, shares->nfs_config);
}

static void
nautilus_shares_init (NautilusShares *shares)
{
  shares->paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  shares->session = oobs_session_get ();

  if (oobs_session_get_connected (shares->session))
    {
      shares->smb_config = oobs_smb_config_get ();
      g_signal_connect_swapped (G_OBJECT (shares->smb_config), "changed",
				G_CALLBACK (nautilus_shares_update_object), shares);

      shares->nfs_config = oobs_nfs_config_get ();
      g_signal_connect_swapped (G_OBJECT (shares->nfs_config), "changed",
				G_CALLBACK (nautilus_shares_update_object), shares);

      nautilus_shares_update (shares);
    }
}

void
nautilus_shares_register_type (GTypeModule *module)
{
  if (!type)
    {
      static const GTypeInfo info =
	{
	  sizeof (NautilusSharesClass),
	  (GBaseInitFunc) NULL,
	  (GBaseFinalizeFunc) NULL,
	  (GClassInitFunc) NULL,
	  NULL,
	  NULL,
	  sizeof (NautilusShares),
	  0,
	  (GInstanceInitFunc) nautilus_shares_init,
	};

      static const GInterfaceInfo menu_provider_iface_info =
	{
	  (GInterfaceInitFunc) menu_provider_iface_init,
	  NULL,
	  NULL
	};

      static const GInterfaceInfo info_provider_iface_info = 
	{
	  (GInterfaceInitFunc) info_provider_iface_init,
	  NULL,
	  NULL
	};

      type = g_type_module_register_type (module,
					  G_TYPE_OBJECT,
					  "NautilusShares",
					  &info, 0);
      g_type_module_add_interface (module,
				   type,
				   NAUTILUS_TYPE_MENU_PROVIDER,
				   &menu_provider_iface_info);

      g_type_module_add_interface (module,
				   type,
				   NAUTILUS_TYPE_INFO_PROVIDER,
				   &info_provider_iface_info);
    }
}

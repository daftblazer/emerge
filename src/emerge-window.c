#include "emerge-window.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <glib/gspawn.h>
#include <sys/stat.h>

struct _EmergeWindow
{
  AdwApplicationWindow  parent_instance;

  /* Template widgets */
  GtkHeaderBar        *header_bar;
  GtkPicture          *output_image;
  GtkEntry            *prompt_entry;
  GtkEntry            *negative_prompt_entry;
  GtkSpinButton       *width_spin;
  GtkSpinButton       *height_spin;
  GtkSpinButton       *steps_spin;
  GtkSpinButton       *seed_spin;
  GtkSpinButton       *cfg_scale_spin;
  GtkDropDown         *sampling_method_dropdown;
  GtkButton           *generate_button;
  GtkButton           *stop_button;
  GtkButton           *save_button;
  GtkSpinner          *spinner;
  AdwToastOverlay     *toast_overlay;
  GtkButton           *model_chooser;
  GtkBox              *advanced_settings_box;
  GtkToggleButton     *advanced_settings_toggle;
  GtkButton           *initial_image_chooser;
  AdwSwitchRow        *img2img_toggle;
  GtkSpinButton       *strength_spin;
  GtkLabel            *status_label;
  GtkButton           *convert_model_button;
  GtkDropDown         *quantization_dropdown;
  GtkWidget           *quantization_label;

  /* Generation state */
  GPid                child_pid;
  guint               child_watch_id;
  GCancellable       *cancellable;
  gchar              *output_path;
  gchar              *model_path;
  gchar              *initial_image_path;
  gboolean            is_generating;
  guint               image_counter;
  gchar              *last_saved_dir;
};

G_DEFINE_TYPE (EmergeWindow, emerge_window, ADW_TYPE_APPLICATION_WINDOW)

static void
process_finished_cb (GPid pid, gint status, gpointer user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  
  /* Re-enable UI */
  self->is_generating = FALSE;
  gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
  gtk_spinner_stop (self->spinner);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  
  if (g_cancellable_is_cancelled (self->cancellable)) {
    gtk_label_set_text (self->status_label, "Cancelled");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Generation cancelled"));
  } else if (status == 0) {
    /* Load the generated image - with refresh to prevent caching issues */
    
    // Print debug info
    g_print("Loading image from: %s\n", self->output_path);
    g_print("File exists: %s\n", g_file_test(self->output_path, G_FILE_TEST_EXISTS) ? "YES" : "NO");
    
    GFile *file = g_file_new_for_path (self->output_path);
    
    /* Clear the current picture first */
    gtk_picture_set_file (self->output_image, NULL);
    
    /* Load the new image */
    GError *load_error = NULL;
    GdkTexture *texture = gdk_texture_new_from_file(file, &load_error);
    if (texture) {
      gtk_picture_set_paintable(self->output_image, GDK_PAINTABLE(texture));
      g_object_unref(texture);
      g_print("Image loaded successfully\n");
    } else {
      g_print("Failed to load texture: %s\n", load_error ? load_error->message : "unknown error");
      if (load_error) g_error_free(load_error);
      
      // Fall back to regular file loading
      gtk_picture_set_file(self->output_image, file);
      g_print("Attempted fallback to gtk_picture_set_file\n");
    }
    
    g_object_unref (file);
    
    /* Show the save button since we have an image now */
    gtk_widget_set_visible (GTK_WIDGET (self->save_button), TRUE);
    
    gtk_label_set_text (self->status_label, "Done");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Image generated successfully"));
  } else {
    gtk_label_set_text (self->status_label, "Failed");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Generation failed"));
  }
  
  g_spawn_close_pid (pid);
  g_object_unref (self->cancellable);
  self->cancellable = NULL;
  self->child_pid = 0;
  self->child_watch_id = 0;
}

static void
model_open_response (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file;
  GError *error = NULL;
  
  file = gtk_file_dialog_open_finish (dialog, result, &error);
  if (file == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to open file: %s", error->message);
    g_error_free (error);
    return;
  }
  
  g_free (self->model_path);
  self->model_path = g_file_get_path (file);
  
  /* Update the button label to show filename */
  char *basename = g_file_get_basename (file);
  char *button_text = g_strdup_printf ("Model: %s", basename);
  gtk_button_set_label (self->model_chooser, button_text);
  g_free (button_text);
  g_free (basename);
  
  /* Enable convert button and show quantization dropdown if it's a safetensors file */
  if (g_str_has_suffix (self->model_path, ".safetensors")) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->quantization_dropdown), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->quantization_label), TRUE);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->quantization_dropdown), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->quantization_label), FALSE);
  }
  
  g_object_unref (file);
}

static void
on_model_file_select (GtkButton *button G_GNUC_UNUSED,
                     gpointer   user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog;
  GtkFileFilter *filter;
  GListStore *filters;
  
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Select Stable Diffusion Model");
  
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "Stable Diffusion Models");
  gtk_file_filter_add_pattern (filter, "*.ckpt");
  gtk_file_filter_add_pattern (filter, "*.safetensors");
  gtk_file_filter_add_pattern (filter, "*.gguf");
  
  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  
  gtk_file_dialog_open (dialog,
                       GTK_WINDOW (self),
                       NULL,  /* cancellable */
                       model_open_response,
                       self);
  
  g_object_unref (dialog);
  g_object_unref (filter);
  g_object_unref (filters);
}

static void
image_open_response (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file;
  GError *error = NULL;
  
  file = gtk_file_dialog_open_finish (dialog, result, &error);
  if (file == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to open file: %s", error->message);
    g_error_free (error);
    return;
  }
  
  g_free (self->initial_image_path);
  self->initial_image_path = g_file_get_path (file);
  
  /* Update the button label to show filename */
  char *basename = g_file_get_basename (file);
  char *button_text = g_strdup_printf ("Image: %s", basename);
  gtk_button_set_label (self->initial_image_chooser, button_text);
  g_free (button_text);
  g_free (basename);
  
  /* Show the initial image with improved loading method */
  gtk_picture_set_file (self->output_image, NULL);
  
  GdkTexture *texture = gdk_texture_new_from_file(file, NULL);
  if (texture) {
    gtk_picture_set_paintable(self->output_image, GDK_PAINTABLE(texture));
    g_object_unref(texture);
  } else {
    gtk_picture_set_file(self->output_image, file);
  }
  
  g_object_unref (file);
}

static void
on_initial_image_file_select (GtkButton *button G_GNUC_UNUSED,
                             gpointer   user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog;
  GtkFileFilter *filter;
  GListStore *filters;
  
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Select Input Image");
  
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "Images");
  gtk_file_filter_add_mime_type (filter, "image/png");
  gtk_file_filter_add_mime_type (filter, "image/jpeg");
  
  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  
  gtk_file_dialog_open (dialog,
                       GTK_WINDOW (self),
                       NULL,  /* cancellable */
                       image_open_response,
                       self);
  
  g_object_unref (dialog);
  g_object_unref (filter);
  g_object_unref (filters);
}

static void
save_image_response (GObject *source_object,
                    GAsyncResult *result,
                    gpointer user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file;
  GError *error = NULL;
  
  file = gtk_file_dialog_save_finish (dialog, result, &error);
  if (file == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to save file: %s", error->message);
    g_error_free (error);
    return;
  }
  
  // Get the directory for future saves
  GFile *parent = g_file_get_parent(file);
  if (parent) {
    g_free(self->last_saved_dir);
    self->last_saved_dir = g_file_get_path(parent);
    g_object_unref(parent);
  }
  
  // Save current image to the selected location
  GFile *src_file = g_file_new_for_path(self->output_path);
  if (src_file) {
    g_file_copy(src_file, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
    g_object_unref(src_file);
    
    if (error) {
      adw_toast_overlay_add_toast(self->toast_overlay,
                                adw_toast_new ("Failed to save image"));
      g_error_free(error);
    } else {
      adw_toast_overlay_add_toast(self->toast_overlay,
                                adw_toast_new ("Image saved successfully"));
    }
  }
  
  g_object_unref(file);
}

static void
on_save_clicked (GtkButton *button G_GNUC_UNUSED,
                gpointer   user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog;
  GtkFileFilter *filter;
  GListStore *filters;
  GFile *current_folder = NULL;
  
  // If there's no image to save, don't open dialog
  if (self->output_path == NULL) {
    adw_toast_overlay_add_toast(self->toast_overlay,
                              adw_toast_new ("No image to save"));
    return;
  }
  
  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save Image");
  
  // Set default name
  gchar *filename = g_strdup_printf("stable-diffusion-%04d.png", self->image_counter - 1);
  gtk_file_dialog_set_initial_name(dialog, filename);
  g_free(filename);
  
  // Set initial folder if we have saved before
  if (self->last_saved_dir) {
    current_folder = g_file_new_for_path(self->last_saved_dir);
  } else {
    // Default to Pictures directory if no previous save location
    const gchar *home_dir = g_get_home_dir();
    gchar *pictures_dir = g_build_filename(home_dir, "Pictures", NULL);
    current_folder = g_file_new_for_path(pictures_dir);
    g_free(pictures_dir);
  }
  
  if (current_folder) {
    gtk_file_dialog_set_initial_folder(dialog, current_folder);
  }
  
  filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "PNG Images");
  gtk_file_filter_add_mime_type(filter, "image/png");
  
  filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  
  gtk_file_dialog_save(dialog,
                      GTK_WINDOW(self),
                      NULL,  /* cancellable */
                      save_image_response,
                      self);
  
  if (current_folder)
    g_object_unref(current_folder);
  g_object_unref(dialog);
  g_object_unref(filter);
  g_object_unref(filters);
}

static gboolean
ensure_directory_exists(const gchar *dir_path)
{
  if (g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
    return TRUE;
  }
  
  // Create directory with permissive permissions so files can be read
  gint result = g_mkdir_with_parents(dir_path, 0755);
  
  return (result == 0);
}

static gchar*
find_sd_executable(void)
{
  gchar *sd_path;
  
  // First try to find 'sd' in PATH (this will work for AppImage)
  sd_path = g_find_program_in_path("sd");
  if (sd_path != NULL) {
    return sd_path;
  }
  
  // If not found in PATH, try in the bin directory relative to the executable
  gchar *exe_dir = NULL;
  gchar *bin_sd_path = NULL;
  
  // Get the directory where our executable is located
  GFile *exe_file = g_file_new_for_path("/proc/self/exe");
  GFile *exe_dir_file = NULL;
  
  if (exe_file) {
    GFileInfo *info = g_file_query_info(exe_file, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
                                       G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info) {
      const char *target = g_file_info_get_attribute_byte_string(info, 
                                                              G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET);
      if (target) {
        GFile *target_file = g_file_new_for_path(target);
        if (target_file) {
          exe_dir_file = g_file_get_parent(target_file);
          g_object_unref(target_file);
        }
      }
      g_object_unref(info);
    }
    
    if (exe_dir_file == NULL) {
      // Fallback if we couldn't read the symlink target
      exe_dir_file = g_file_get_parent(exe_file);
    }
    g_object_unref(exe_file);
  }
  
  // Initialize paths to try
  GQueue *paths_to_try = g_queue_new();
  
  // Add the current directory
  g_queue_push_tail(paths_to_try, g_strdup("."));
  
  if (exe_dir_file) {
    exe_dir = g_file_get_path(exe_dir_file);
    g_object_unref(exe_dir_file);
    
    if (exe_dir) {
      // Add executable directory
      g_queue_push_tail(paths_to_try, g_strdup(exe_dir));
      
      // Also try one level up from executable directory
      GFile *parent_dir_file = g_file_new_for_path(exe_dir);
      GFile *project_dir_file = g_file_get_parent(parent_dir_file);
      g_object_unref(parent_dir_file);
      
      if (project_dir_file) {
        gchar *project_dir = g_file_get_path(project_dir_file);
        g_object_unref(project_dir_file);
        
        if (project_dir) {
          // Add parent directory
          g_queue_push_tail(paths_to_try, g_strdup(project_dir));
          
          // For build directory scenarios, try two and three levels up
          // This handles cases like build/src/emerge where we need to go up to find project root
          GFile *build_dir_file = g_file_new_for_path(project_dir);
          GFile *root_dir_file = g_file_get_parent(build_dir_file);
          g_object_unref(build_dir_file);
          
          if (root_dir_file) {
            gchar *root_dir = g_file_get_path(root_dir_file);
            g_queue_push_tail(paths_to_try, g_strdup(root_dir));
            
            // Try one more level up
            GFile *root_parent_file = g_file_get_parent(root_dir_file);
            g_object_unref(root_dir_file);
            
            if (root_parent_file) {
              gchar *root_parent = g_file_get_path(root_parent_file);
              g_queue_push_tail(paths_to_try, g_strdup(root_parent));
              g_object_unref(root_parent_file);
              g_free(root_parent);
            }
            
            g_free(root_dir);
          }
          
          g_free(project_dir);
        }
      }
      
      g_free(exe_dir);
    }
  }
  
  // Try each path to see if bin/sd exists there
  while (!g_queue_is_empty(paths_to_try)) {
    gchar *base_path = g_queue_pop_head(paths_to_try);
    
    // Try bin/sd in this location
    bin_sd_path = g_build_filename(base_path, "bin", "sd", NULL);
    g_print("Checking for sd at: %s\n", bin_sd_path);
    
    if (g_file_test(bin_sd_path, G_FILE_TEST_IS_EXECUTABLE)) {
      // Free remaining paths
      while (!g_queue_is_empty(paths_to_try)) {
        g_free(g_queue_pop_head(paths_to_try));
      }
      g_queue_free(paths_to_try);
      g_free(base_path);
      return bin_sd_path;
    }
    
    g_free(bin_sd_path);
    
    // Try just 'sd' in this location (for development builds)
    bin_sd_path = g_build_filename(base_path, "sd", NULL);
    g_print("Checking for sd at: %s\n", bin_sd_path);
    
    if (g_file_test(bin_sd_path, G_FILE_TEST_IS_EXECUTABLE)) {
      // Free remaining paths
      while (!g_queue_is_empty(paths_to_try)) {
        g_free(g_queue_pop_head(paths_to_try));
      }
      g_queue_free(paths_to_try);
      g_free(base_path);
      return bin_sd_path;
    }
    g_free(bin_sd_path);
    
    g_free(base_path);
  }
  
  g_queue_free(paths_to_try);
  return NULL;
}

static void
on_generate_clicked (GtkButton *button G_GNUC_UNUSED,
                     gpointer   user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  gchar *command_line;
  gchar **argv = NULL;
  GError *error = NULL;
  gint argc;
  gchar *sd_path;
  
  if (self->is_generating)
    return;
  
  if (self->model_path == NULL) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Please select a model file"));
    return;
  }
  
  // Free previous output path if it exists
  g_free(self->output_path);
  
  // Get system temp directory and create a unique subdirectory for emerge
  const gchar *temp_dir = g_get_tmp_dir();
  gchar *emerge_temp_dir = g_build_filename(temp_dir, "emerge-temp", NULL);
  
  // Ensure the directory exists with proper permissions
  if (!ensure_directory_exists(emerge_temp_dir)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Failed to create temporary directory"));
    g_free(emerge_temp_dir);
    return;
  }
  
  // Create sequentially numbered output path in the temp directory
  gchar *filename = g_strdup_printf("emerge-output-%04d.png", self->image_counter++);
  self->output_path = g_build_filename(emerge_temp_dir, filename, NULL);
  g_free(filename);
  
  // Make sure permissions are correct on the temp directory for writing
  chmod(emerge_temp_dir, 0755);
  g_free(emerge_temp_dir);
  
  g_print("Will save output to: %s\n", self->output_path);
  
  /* Find the sd binary in PATH or in bin directory */
  sd_path = find_sd_executable();
  
  if (sd_path == NULL) {
    // Create a more detailed error message
    GString *error_msg = g_string_new("Failed to find 'sd' executable. Install paths checked:\n");
    
    // Add current directory to the error message
    gchar *cwd = g_get_current_dir();
    g_string_append_printf(error_msg, "- %s/bin/sd\n", cwd);
    g_string_append_printf(error_msg, "- %s/sd\n", cwd);
    g_free(cwd);
    
    // Add paths relative to executable
    gchar *exe_path = NULL;
    GFile *exe_file = g_file_new_for_path("/proc/self/exe");
    if (exe_file) {
      exe_path = g_file_get_path(exe_file);
      g_object_unref(exe_file);
      if (exe_path) {
        gchar *exe_dir = g_path_get_dirname(exe_path);
        g_string_append_printf(error_msg, "- %s/bin/sd\n", exe_dir);
        g_string_append_printf(error_msg, "- %s/sd\n", exe_dir);
        
        gchar *parent_dir = g_path_get_dirname(exe_dir);
        g_string_append_printf(error_msg, "- %s/bin/sd\n", parent_dir);
        g_string_append_printf(error_msg, "- %s/sd\n", parent_dir);
        
        gchar *root_dir = g_path_get_dirname(parent_dir);
        g_string_append_printf(error_msg, "- %s/bin/sd\n", root_dir);
        g_string_append_printf(error_msg, "- %s/sd\n", root_dir);
        
        g_free(root_dir);
        g_free(parent_dir);
        g_free(exe_dir);
        g_free(exe_path);
      }
    }
    
    g_string_append(error_msg, "Make sure the 'sd' executable is in one of these locations or in PATH.");
    
    gtk_label_set_text (self->status_label, "Failed to find sd");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error_msg->str));
    g_string_free(error_msg, TRUE);
    
    // Re-enable relevant UI elements if needed, similar to other error paths
    self->is_generating = FALSE;
    gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    // etc. for other UI elements if they were disabled before this check
    return;
  }
  
  /* Prepare the command line */
  if (adw_switch_row_get_active (self->img2img_toggle)) {
    if (self->initial_image_path == NULL) {
      adw_toast_overlay_add_toast (self->toast_overlay,
                                 adw_toast_new ("Please select an initial image for img2img"));
      g_free(sd_path);
      return;
    }
    
    command_line = g_strdup_printf ("\"%s\" --mode img2img --model \"%s\" --prompt \"%s\" --negative-prompt \"%s\" --width %d --height %d --steps %d --seed %ld --cfg-scale %.1f --sampling-method %s --output \"%s\" --input \"%s\" --strength %.2f --vae-tiling",
                                    sd_path,
                                    self->model_path,
                                    gtk_editable_get_text (GTK_EDITABLE (self->prompt_entry)),
                                    gtk_editable_get_text (GTK_EDITABLE (self->negative_prompt_entry)),
                                    (int) gtk_spin_button_get_value (self->width_spin),
                                    (int) gtk_spin_button_get_value (self->height_spin),
                                    (int) gtk_spin_button_get_value (self->steps_spin),
                                    (long) gtk_spin_button_get_value (self->seed_spin),
                                    gtk_spin_button_get_value (self->cfg_scale_spin),
                                    gtk_string_object_get_string (GTK_STRING_OBJECT (
                                      gtk_drop_down_get_selected_item (self->sampling_method_dropdown))),
                                    self->output_path,
                                    self->initial_image_path,
                                    gtk_spin_button_get_value (self->strength_spin));
  } else {
    command_line = g_strdup_printf ("\"%s\" --model \"%s\" --prompt \"%s\" --negative-prompt \"%s\" --width %d --height %d --steps %d --seed %ld --cfg-scale %.1f --sampling-method %s --output \"%s\" --vae-tiling",
                                    sd_path,
                                    self->model_path,
                                    gtk_editable_get_text (GTK_EDITABLE (self->prompt_entry)),
                                    gtk_editable_get_text (GTK_EDITABLE (self->negative_prompt_entry)),
                                    (int) gtk_spin_button_get_value (self->width_spin),
                                    (int) gtk_spin_button_get_value (self->height_spin),
                                    (int) gtk_spin_button_get_value (self->steps_spin),
                                    (long) gtk_spin_button_get_value (self->seed_spin),
                                    gtk_spin_button_get_value (self->cfg_scale_spin),
                                    gtk_string_object_get_string (GTK_STRING_OBJECT (
                                      gtk_drop_down_get_selected_item (self->sampling_method_dropdown))),
                                    self->output_path);
  }
  
  g_free(sd_path);
  
  /* Disable UI while generating */
  self->is_generating = TRUE;
  gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->stop_button), TRUE);
  gtk_spinner_start (self->spinner);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), TRUE);
  gtk_label_set_text (self->status_label, "Generating...");
  
  /* Start the process */
  self->cancellable = g_cancellable_new ();
  
  // Parse command_line into argv
  if (!g_shell_parse_argv (command_line, &argc, &argv, &error)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new_format ("Error parsing command line: %s", error->message));
    g_error_free (error);
    g_free (command_line);
    // Re-enable UI (copied from existing error handling)
    self->is_generating = FALSE;
    gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
    gtk_spinner_stop (self->spinner);
    gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
    gtk_label_set_text (self->status_label, "Ready");
    g_object_unref(self->cancellable); // also unref cancellable here
    self->cancellable = NULL;
    return;
  }
  
  if (!g_spawn_async_with_pipes (NULL, argv, NULL, 
                                G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                NULL, NULL, &self->child_pid, 
                                NULL, NULL, NULL, &error)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error->message));
    g_error_free (error);
    g_free (command_line);
    g_strfreev (argv);
    
    /* Re-enable UI */
    self->is_generating = FALSE;
    gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
    gtk_spinner_stop (self->spinner);
    gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
    gtk_label_set_text (self->status_label, "Ready");
    
    return;
  }
  
  g_free (command_line);
  g_strfreev (argv);
  
  /* Monitor the process */
  self->child_watch_id = g_child_watch_add (self->child_pid, 
                                          process_finished_cb, self);
}

static void
on_stop_clicked (GtkButton *button G_GNUC_UNUSED,
                 gpointer   user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  
  if (self->child_pid != 0) {
    g_cancellable_cancel (self->cancellable);
    kill (self->child_pid, SIGTERM);
  }
}

static void
on_img2img_toggled (AdwSwitchRow *button,
                    gpointer         user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  gboolean active = adw_switch_row_get_active (button);
  
  gtk_widget_set_visible (GTK_WIDGET (self->initial_image_chooser), active);
  gtk_widget_set_visible (GTK_WIDGET (self->strength_spin), active);
  gtk_widget_set_sensitive (GTK_WIDGET (self->strength_spin), active);
}

static void
on_advanced_settings_toggled (GtkToggleButton *button,
                             gpointer         user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  gboolean active = gtk_toggle_button_get_active (button);
  
  gtk_widget_set_visible (GTK_WIDGET (self->advanced_settings_box), active);
}

static void
convert_process_finished_cb (GPid pid, gint status, gpointer user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  
  /* Re-enable UI */
  gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
  gtk_spinner_stop (self->spinner);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  
  if (g_cancellable_is_cancelled (self->cancellable)) {
    gtk_label_set_text (self->status_label, "Cancelled");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Conversion cancelled"));
  } else if (status == 0) {
    gtk_label_set_text (self->status_label, "Done");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Model converted successfully"));
  } else {
    gtk_label_set_text (self->status_label, "Failed");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Model conversion failed"));
  }
  
  g_spawn_close_pid (pid);
  g_object_unref (self->cancellable);
  self->cancellable = NULL;
  self->child_pid = 0;
  self->child_watch_id = 0;
}

static void
convert_model_save_response (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file;
  GError *error = NULL;
  gchar *output_path;
  const char *quant_type;
  gchar *command_line;
  gchar **argv = NULL;
  gint argc;
  gchar *sd_path;
  
  file = gtk_file_dialog_save_finish (dialog, result, &error);
  if (file == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to save file: %s", error->message);
    g_error_free (error);
    return;
  }
  
  output_path = g_file_get_path (file);
  
  /* Get quantization type from dropdown */
  GtkStringObject *selected = gtk_drop_down_get_selected_item (self->quantization_dropdown);
  quant_type = "q8_0"; /* Default to q8_0 */
  if (selected) {
    quant_type = gtk_string_object_get_string (selected);
  }
  
  /* Set up UI for conversion */
  gtk_spinner_start (self->spinner);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), TRUE);
  gtk_label_set_text (self->status_label, "Converting model...");
  gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), FALSE);
  
  /* Find the sd binary in PATH or bin directory */
  sd_path = find_sd_executable();
  
  if (sd_path == NULL) {
    // Create a more detailed error message
    GString *error_msg = g_string_new("Failed to find 'sd' executable. Install paths checked:\n");
    
    // Add current directory to the error message
    gchar *cwd = g_get_current_dir();
    g_string_append_printf(error_msg, "- %s/bin/sd\n", cwd);
    g_string_append_printf(error_msg, "- %s/sd\n", cwd);
    g_free(cwd);
    
    // Add paths relative to executable
    gchar *exe_path = NULL;
    GFile *exe_file = g_file_new_for_path("/proc/self/exe");
    if (exe_file) {
      exe_path = g_file_get_path(exe_file);
      g_object_unref(exe_file);
      if (exe_path) {
        gchar *exe_dir = g_path_get_dirname(exe_path);
        g_string_append_printf(error_msg, "- %s/bin/sd\n", exe_dir);
        g_string_append_printf(error_msg, "- %s/sd\n", exe_dir);
        
        gchar *parent_dir = g_path_get_dirname(exe_dir);
        g_string_append_printf(error_msg, "- %s/bin/sd\n", parent_dir);
        g_string_append_printf(error_msg, "- %s/sd\n", parent_dir);
        
        gchar *root_dir = g_path_get_dirname(parent_dir);
        g_string_append_printf(error_msg, "- %s/bin/sd\n", root_dir);
        g_string_append_printf(error_msg, "- %s/sd\n", root_dir);
        
        g_free(root_dir);
        g_free(parent_dir);
        g_free(exe_dir);
        g_free(exe_path);
      }
    }
    
    g_string_append(error_msg, "Make sure the 'sd' executable is in one of these locations or in PATH.");
    
    gtk_label_set_text (self->status_label, "Failed to find sd");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error_msg->str));
    g_string_free(error_msg, TRUE);
    
    g_free (output_path);
    g_object_unref (file);
    // Reset UI (copied from existing error handling)
    gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    gtk_spinner_stop (self->spinner);
    gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
    gtk_label_set_text (self->status_label, "Ready");
    return;
  }
  
  /* Prepare the command line */
  command_line = g_strdup_printf ("\"%s\" -M convert -m \"%s\" -o \"%s\" -v --type %s",
                                 sd_path,
                                 self->model_path,
                                 output_path,
                                 quant_type);
  
  g_free(sd_path);
  
  /* Parse the command line */
  if (!g_shell_parse_argv (command_line, &argc, &argv, &error)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error->message));
    g_error_free (error);
    g_free (command_line);
    g_strfreev (argv);
    g_free (output_path);
    g_object_unref (file);
    
    /* Reset UI */
    gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    gtk_spinner_stop (self->spinner);
    gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
    gtk_label_set_text (self->status_label, "Ready");
    
    return;
  }
  
  /* Start the process */
  self->cancellable = g_cancellable_new ();
  
  if (!g_spawn_async_with_pipes (NULL, argv, NULL, 
                                G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                NULL, NULL, &self->child_pid, 
                                NULL, NULL, NULL, &error)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error->message));
    g_error_free (error);
    g_free (command_line);
    g_strfreev (argv);
    g_free (output_path);
    g_object_unref (file);
    
    /* Reset UI */
    gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    gtk_spinner_stop (self->spinner);
    gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
    gtk_label_set_text (self->status_label, "Ready");
    
    return;
  }
  
  g_free (command_line);
  g_strfreev (argv);
  g_free (output_path);
  g_object_unref (file);
  
  /* Monitor the process */
  self->child_watch_id = g_child_watch_add (self->child_pid, 
                                          convert_process_finished_cb, self);
}

static void
on_convert_model_clicked (GtkButton *button G_GNUC_UNUSED,
                        gpointer   user_data)
{
  EmergeWindow *self = EMERGE_WINDOW (user_data);
  GtkFileDialog *dialog;
  GtkFileFilter *filter;
  GListStore *filters;
  gchar *suggested_filename;
  
  /* Check if a model has been selected */
  if (self->model_path == NULL) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Please select a model file first"));
    return;
  }
  
  /* Check if it's a safetensors file */
  if (!g_str_has_suffix (self->model_path, ".safetensors")) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Only .safetensors files can be converted"));
    return;
  }
  
  /* Get quantization type for suggested filename */
  GtkStringObject *selected = gtk_drop_down_get_selected_item (self->quantization_dropdown);
  const char *quant_type = "q8_0"; /* Default to q8_0 */
  if (selected) {
    quant_type = gtk_string_object_get_string (selected);
  }
  
  /* Create file save dialog */
  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save Converted Model");
  
  /* Get base filename without the extension */
  gchar *basename = g_path_get_basename(self->model_path);
  gchar *base_without_ext = g_strndup(basename, strlen(basename) - strlen(".safetensors"));
  
  /* Set default name */
  suggested_filename = g_strdup_printf("%s.%s.gguf", base_without_ext, quant_type);
  gtk_file_dialog_set_initial_name(dialog, suggested_filename);
  g_free(suggested_filename);
  g_free(base_without_ext);
  g_free(basename);
  
  /* Set initial folder to same directory as source model */
  gchar *dir_path = g_path_get_dirname(self->model_path);
  GFile *initial_folder = g_file_new_for_path(dir_path);
  gtk_file_dialog_set_initial_folder(dialog, initial_folder);
  g_object_unref(initial_folder);
  g_free(dir_path);
  
  /* Set up filter for GGUF files */
  filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "GGUF Model Files");
  gtk_file_filter_add_pattern(filter, "*.gguf");
  
  filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  
  /* Show the save dialog */
  gtk_file_dialog_save(dialog,
                      GTK_WINDOW(self),
                      NULL,  /* cancellable */
                      convert_model_save_response,
                      self);
  
  g_object_unref(dialog);
  g_object_unref(filter);
  g_object_unref(filters);
}

static void
remove_directory_contents(const gchar *dir_path)
{
  GDir *dir;
  GError *error = NULL;
  const gchar *filename;
  
  dir = g_dir_open(dir_path, 0, &error);
  if (dir == NULL) {
    g_warning("Failed to open directory %s: %s", dir_path, error->message);
    g_error_free(error);
    return;
  }
  
  while ((filename = g_dir_read_name(dir)) != NULL) {
    gchar *file_path = g_build_filename(dir_path, filename, NULL);
    
    if (g_file_test(file_path, G_FILE_TEST_IS_DIR)) {
      // Recursively delete subdirectories
      remove_directory_contents(file_path);
      rmdir(file_path);
    } else {
      // Delete file
      if (unlink(file_path) != 0) {
        g_warning("Failed to delete file: %s", file_path);
      }
    }
    
    g_free(file_path);
  }
  
  g_dir_close(dir);
}

static void
emerge_window_finalize (GObject *object)
{
  EmergeWindow *self = EMERGE_WINDOW (object);
  
  if (self->child_pid != 0) {
    kill (self->child_pid, SIGTERM);
    g_spawn_close_pid (self->child_pid);
    g_source_remove (self->child_watch_id);
  }
  
  // Clean up temporary directory
  const gchar *temp_dir = g_get_tmp_dir();
  gchar *emerge_temp_dir = g_build_filename(temp_dir, "emerge-temp", NULL);
  
  // Remove contents of directory
  if (g_file_test(emerge_temp_dir, G_FILE_TEST_IS_DIR)) {
    remove_directory_contents(emerge_temp_dir);
    // Try to remove the directory itself
    rmdir(emerge_temp_dir);
  }
  
  g_free(emerge_temp_dir);
  g_free (self->output_path);
  g_free (self->model_path);
  g_free (self->initial_image_path);
  g_free (self->last_saved_dir);
  
  if (self->cancellable != NULL)
    g_object_unref (self->cancellable);
  
  G_OBJECT_CLASS (emerge_window_parent_class)->finalize (object);
}

static void
emerge_window_class_init (EmergeWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  
  object_class->finalize = emerge_window_finalize;
  
  gtk_widget_class_set_template_from_resource (widget_class, "/com/github/emerge/window.ui");
  
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, output_image);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, prompt_entry);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, negative_prompt_entry);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, width_spin);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, height_spin);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, steps_spin);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, seed_spin);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, cfg_scale_spin);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, sampling_method_dropdown);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, generate_button);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, stop_button);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, save_button);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, spinner);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, model_chooser);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, advanced_settings_box);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, advanced_settings_toggle);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, initial_image_chooser);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, img2img_toggle);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, strength_spin);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, status_label);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, convert_model_button);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, quantization_dropdown);
  gtk_widget_class_bind_template_child (widget_class, EmergeWindow, quantization_label);
  
  gtk_widget_class_bind_template_callback (widget_class, on_generate_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_stop_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_save_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_model_file_select);
  gtk_widget_class_bind_template_callback (widget_class, on_initial_image_file_select);
  gtk_widget_class_bind_template_callback (widget_class, on_img2img_toggled);
  gtk_widget_class_bind_template_callback (widget_class, on_advanced_settings_toggled);
  gtk_widget_class_bind_template_callback (widget_class, on_convert_model_clicked);
}

static void
emerge_window_init (EmergeWindow *self)
{
  GtkStringList *sampling_methods;
  
  gtk_widget_init_template (GTK_WIDGET (self));
  
  self->is_generating = FALSE;
  self->child_pid = 0;
  self->child_watch_id = 0;
  self->cancellable = NULL;
  self->output_path = NULL;
  self->model_path = NULL;
  self->initial_image_path = NULL;
  self->image_counter = 1;
  self->last_saved_dir = NULL;
  
  /* Initialize UI values */
  gtk_spin_button_set_value (self->width_spin, 512);
  gtk_spin_button_set_value (self->height_spin, 512);
  gtk_spin_button_set_value (self->steps_spin, 20);
  gtk_spin_button_set_value (self->seed_spin, -1);  /* -1 means random seed */
  gtk_spin_button_set_value (self->cfg_scale_spin, 7.0);
  gtk_spin_button_set_value (self->strength_spin, 0.75);
  
  /* Set up the sampling method dropdown */
  sampling_methods = gtk_string_list_new (NULL);
  
  gtk_string_list_append (sampling_methods, "euler");
  gtk_string_list_append (sampling_methods, "euler_a");
  gtk_string_list_append (sampling_methods, "heun");
  gtk_string_list_append (sampling_methods, "dpm2");
  gtk_string_list_append (sampling_methods, "dpm++2s_a");
  gtk_string_list_append (sampling_methods, "dpm++2m");
  gtk_string_list_append (sampling_methods, "dpm++2mv2");
  gtk_string_list_append (sampling_methods, "lcm");
  
  gtk_drop_down_set_model (self->sampling_method_dropdown, G_LIST_MODEL (sampling_methods));
  gtk_drop_down_set_selected (self->sampling_method_dropdown, 1); /* Default to euler_a */
  
  /* Hide advanced settings by default */
  gtk_widget_set_visible (GTK_WIDGET (self->advanced_settings_box), FALSE);
  
  /* Hide img2img widgets by default */
  gtk_widget_set_visible (GTK_WIDGET (self->initial_image_chooser), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->strength_spin), FALSE);
  
  /* Hide stop button and spinner by default */
  gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  
  /* Hide save button by default (until we have an image) */
  gtk_widget_set_visible (GTK_WIDGET (self->save_button), FALSE);
  
  /* Hide conversion-related UI elements by default */
  gtk_widget_set_visible (GTK_WIDGET (self->quantization_dropdown), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->quantization_label), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->convert_model_button), FALSE);
  
  gtk_label_set_text (self->status_label, "Ready");
  
  /* Set up quantization options for GGUF conversion */
  GtkStringList *quant_types = gtk_string_list_new (NULL);
  const char * const quant_type_names[] = {
    "f16", "f32", "q8_0", "q5_0", "q5_1", "q4_0", "q4_1", NULL
  };
  for (int i = 0; quant_type_names[i] != NULL; i++) {
    gtk_string_list_append (quant_types, quant_type_names[i]);
  }
  gtk_drop_down_set_model (self->quantization_dropdown, G_LIST_MODEL (quant_types));
  /* Default to q8_0 (index 2) */
  gtk_drop_down_set_selected (self->quantization_dropdown, 2);
  g_object_unref (quant_types);
} 
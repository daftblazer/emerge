#include "stable-gtk-window.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <glib/gspawn.h>

struct _StableGtkWindow
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
  GtkToggleButton     *img2img_toggle;
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

G_DEFINE_TYPE (StableGtkWindow, stable_gtk_window, ADW_TYPE_APPLICATION_WINDOW)

static void
process_finished_cb (GPid pid, gint status, gpointer user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  
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
    GFile *file = g_file_new_for_path (self->output_path);
    
    /* Clear the current picture first */
    gtk_picture_set_file (self->output_image, NULL);
    
    /* Load the new image */
    GdkTexture *texture = gdk_texture_new_from_file(file, NULL);
    if (texture) {
      gtk_picture_set_paintable(self->output_image, GDK_PAINTABLE(texture));
      g_object_unref(texture);
    } else {
      gtk_picture_set_file(self->output_image, file);
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
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
on_model_file_select (GtkButton *button,
                     gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
on_initial_image_file_select (GtkButton *button,
                             gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
on_save_clicked (GtkButton *button,
                gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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

static void
on_generate_clicked (GtkButton *button,
                     gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
  
  // Create sequentially numbered output path
  self->output_path = g_build_filename (g_get_tmp_dir (), 
                                      g_strdup_printf("stable-gtk-output-%04d.png", self->image_counter++), 
                                      NULL);
  
  /* Get the absolute path to the sd binary */
  sd_path = g_build_filename ("/run/media/system/Projects/Coding/Stable-GTK", "bin", "sd", NULL);
  
  /* Prepare the command line */
  if (gtk_toggle_button_get_active (self->img2img_toggle)) {
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
  
  /* Parse the command line */
  if (!g_shell_parse_argv (command_line, &argc, &argv, &error)) {
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
on_stop_clicked (GtkButton *button,
                 gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  
  if (self->child_pid != 0) {
    g_cancellable_cancel (self->cancellable);
    kill (self->child_pid, SIGTERM);
  }
}

static void
on_img2img_toggled (GtkToggleButton *button,
                    gpointer         user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  gboolean active = gtk_toggle_button_get_active (button);
  
  gtk_widget_set_visible (GTK_WIDGET (self->initial_image_chooser), active);
  gtk_widget_set_visible (GTK_WIDGET (self->strength_spin), active);
  gtk_widget_set_sensitive (GTK_WIDGET (self->strength_spin), active);
}

static void
on_advanced_settings_toggled (GtkToggleButton *button,
                             gpointer         user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  gboolean active = gtk_toggle_button_get_active (button);
  
  gtk_widget_set_visible (GTK_WIDGET (self->advanced_settings_box), active);
}

static void
convert_process_finished_cb (GPid pid, gint status, gpointer user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  
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
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
  
  /* Get the absolute path to the sd binary */
  sd_path = g_build_filename ("/run/media/system/Projects/Coding/Stable-GTK", "bin", "sd", NULL);
  
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
on_convert_model_clicked (GtkButton *button,
                        gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
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
stable_gtk_window_finalize (GObject *object)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (object);
  
  if (self->child_pid != 0) {
    kill (self->child_pid, SIGTERM);
    g_spawn_close_pid (self->child_pid);
    g_source_remove (self->child_watch_id);
  }
  
  g_free (self->output_path);
  g_free (self->model_path);
  g_free (self->initial_image_path);
  g_free (self->last_saved_dir);
  
  if (self->cancellable != NULL)
    g_object_unref (self->cancellable);
  
  G_OBJECT_CLASS (stable_gtk_window_parent_class)->finalize (object);
}

static void
stable_gtk_window_class_init (StableGtkWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  
  object_class->finalize = stable_gtk_window_finalize;
  
  gtk_widget_class_set_template_from_resource (widget_class, "/com/github/stable-gtk/window.ui");
  
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, output_image);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, prompt_entry);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, negative_prompt_entry);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, width_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, height_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, steps_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, seed_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, cfg_scale_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, sampling_method_dropdown);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, generate_button);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, stop_button);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, save_button);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, spinner);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, model_chooser);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, advanced_settings_box);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, advanced_settings_toggle);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, initial_image_chooser);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, img2img_toggle);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, strength_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, status_label);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, convert_model_button);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, quantization_dropdown);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, quantization_label);
  
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
stable_gtk_window_init (StableGtkWindow *self)
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
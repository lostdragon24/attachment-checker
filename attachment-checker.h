#ifndef ATTACHMENT_CHECKER_H
#define ATTACHMENT_CHECKER_H

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

// Ключи для GSettings
#define ATTACHMENT_CHECKER_SCHEMA_ID "org.gnome.evolution.plugin.attachment-checker"
#define ATTACHMENT_CHECKER_PATH "/org/gnome/evolution/plugin/attachment-checker/"

#define KEY_FORBIDDEN_WORDS "forbidden-words"
#define KEY_CHECK_ATTACHMENTS "check-attachments"
#define KEY_CHECK_MESSAGE_BODY "check-message-body"
#define KEY_CASE_SENSITIVE "case-sensitive"

// Структура для UI настроек
typedef struct {
    GSettings *settings;
    GtkWidget *treeview;
    GtkWidget *word_add;
    GtkWidget *word_remove;
    GtkListStore *store;
    GtkWidget *check_attachments;
    GtkWidget *check_message_body;
    GtkWidget *check_case_sensitive;
} UIData;

// Колонки для TreeView
enum {
    WORD_KEYWORD_COLUMN,
    WORD_N_COLUMNS
};

// Прототипы функций
gchar** load_forbidden_words(GSettings *settings);
void save_forbidden_words(GSettings *settings, gchar **words);
gboolean check_text_for_forbidden_words(const gchar *text, gchar **forbidden_words, 
                                         gboolean case_sensitive, gchar **found_word);
gboolean check_attachment_names(EAttachmentStore *store, gchar **forbidden_words,
                                  gboolean case_sensitive, gchar **found_name);

#endif /* ATTACHMENT_CHECKER_H */
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <camel/camel.h>
#include <evolution/e-util/e-util.h>
#include <evolution/mail/em-config.h>
#include <evolution/mail/em-event.h>
#include <evolution/composer/e-msg-composer.h>

#include "attachment-checker.h"

// Объявления функций (прототипы)
static gchar* extract_text_from_camel_data_wrapper(CamelDataWrapper *dw);
static gchar* extract_text_from_camel_part(CamelMimePart *part);
static gchar* get_message_text(EMsgComposer *composer);
static gchar* get_message_text_simple(EMsgComposer *composer);

// Загрузка запрещённых слов из GSettings
gchar**
load_forbidden_words(GSettings *settings)
{
    if (!settings)
        return NULL;
    
    gchar **words = g_settings_get_strv(settings, KEY_FORBIDDEN_WORDS);
    
    // Если нет сохранённых слов, возвращаем значения по умолчанию
    if (!words || !words[0]) {
        static const gchar *default_words[] = {
            "confidential", "secret", "password", 
            "private", "internal", "draft", NULL
        };
        
        g_strfreev(words);
        words = g_strdupv((gchar **)default_words);
    }
    
    return words;
}

// Сохранение запрещённых слов в GSettings
void
save_forbidden_words(GSettings *settings, gchar **words)
{
    if (!settings || !words)
        return;
    
    g_settings_set_strv(settings, KEY_FORBIDDEN_WORDS, (const gchar * const *)words);
    g_settings_sync();
}

// Проверка текста на запрещённые слова
gboolean
check_text_for_forbidden_words(const gchar *text, gchar **forbidden_words, 
                                 gboolean case_sensitive, gchar **found_word)
{
    if (!text || !forbidden_words || !forbidden_words[0])
        return FALSE;
    
    gchar *text_to_search = NULL;
    gchar **words_to_search = NULL;
    gboolean found = FALSE;
    int i;
    
    // Подготовка текста для поиска
    if (!case_sensitive) {
        text_to_search = g_utf8_strdown(text, -1);
    } else {
        text_to_search = g_strdup(text);
    }
    
    // Подготовка слов для поиска
    if (!case_sensitive) {
        words_to_search = g_new0(gchar*, g_strv_length(forbidden_words) + 1);
        for (i = 0; forbidden_words[i]; i++) {
            words_to_search[i] = g_utf8_strdown(forbidden_words[i], -1);
        }
    } else {
        words_to_search = g_strdupv(forbidden_words);
    }
    
    // Поиск
    for (i = 0; words_to_search[i] && !found; i++) {
        if (strstr(text_to_search, words_to_search[i])) {
            found = TRUE;
            if (found_word) {
                *found_word = g_strdup(forbidden_words[i]);
            }
        }
    }
    
    // Очистка
    g_free(text_to_search);
    if (!case_sensitive) {
        g_strfreev(words_to_search);
    } else {
        g_free(words_to_search);
    }
    
    return found;
}

// Проверка имён вложений
gboolean
check_attachment_names(EAttachmentStore *store, gchar **forbidden_words,
                         gboolean case_sensitive, gchar **found_name)
{
    if (!store || !forbidden_words || !forbidden_words[0])
        return FALSE;
    
    GList *attachments = e_attachment_store_get_attachments(store);
    gboolean found = FALSE;
    
    for (GList *item = attachments; item != NULL && !found; item = item->next) {
        EAttachment *attachment = E_ATTACHMENT(item->data);
        GFile *file = e_attachment_ref_file(attachment);
        
        if (file) {
            gchar *basename = g_file_get_basename(file);
            if (basename) {
                found = check_text_for_forbidden_words(basename, forbidden_words,
                                                        case_sensitive, found_name);
                g_free(basename);
            }
            g_object_unref(file);
        }
    }
    
    g_list_free_full(attachments, g_object_unref);
    return found;
}

// Функция для извлечения текста из Camel-объектов
static gchar*
extract_text_from_camel_data_wrapper(CamelDataWrapper *dw)
{
    if (!dw)
        return NULL;
    
    GByteArray *data = g_byte_array_new();
    CamelStream *stream = camel_stream_mem_new_with_byte_array(data);
    GCancellable *cancellable = NULL;
    GError *error = NULL;
    
    // Используем синхронную версию функции
    gssize bytes_written = camel_data_wrapper_decode_to_stream_sync(dw, stream, cancellable, &error);
    
    if (bytes_written >= 0 && error == NULL) {
        if (data->len > 0) {
            gchar *text = g_strndup((gchar *)data->data, data->len);
            g_object_unref(stream);
            g_byte_array_free(data, TRUE);
            return text;
        }
    } else {
        if (error) {
            g_warning("Error decoding data wrapper: %s", error->message);
            g_error_free(error);
        }
    }
    
    g_object_unref(stream);
    g_byte_array_free(data, TRUE);
    return NULL;
}

// Функция для извлечения текста из CamelMimePart
static gchar*
extract_text_from_camel_part(CamelMimePart *part)
{
    if (!part)
        return NULL;
    
    GString *result = g_string_new(NULL);
    
    // Получаем тип содержимого
    CamelContentType *content_type = camel_mime_part_get_content_type(part);
    const gchar *mime_type = camel_content_type_simple(content_type);
    
    g_debug("Processing MIME part of type: %s", mime_type ? mime_type : "unknown");
    
    // Проверяем, является ли часть текстовой
    if (mime_type && g_str_has_prefix(mime_type, "text/")) {
        CamelDataWrapper *dw = camel_medium_get_content(CAMEL_MEDIUM(part));
        if (dw) {
            gchar *text = extract_text_from_camel_data_wrapper(dw);
            if (text) {
                g_string_append(result, text);
                g_free(text);
            }
        }
    }
    // Обрабатываем multipart
    else if (mime_type && g_str_has_prefix(mime_type, "multipart/")) {
        // Проверяем, является ли часть multipart
        if (CAMEL_IS_MULTIPART(part)) {
            CamelMultipart *multipart = CAMEL_MULTIPART(part);
            gint n_parts = camel_multipart_get_number(multipart);
            
            g_debug("Multipart message with %d parts", n_parts);
            
            for (gint i = 0; i < n_parts; i++) {
                CamelMimePart *subpart = camel_multipart_get_part(multipart, i);
                if (subpart) {
                    gchar *subtext = extract_text_from_camel_part(subpart);
                    if (subtext) {
                        g_string_append(result, subtext);
                        g_string_append_c(result, '\n');
                        g_free(subtext);
                    }
                }
            }
        }
    }
    
    if (result->len == 0) {
        g_string_free(result, TRUE);
        return NULL;
    }
    
    return g_string_free(result, FALSE);
}

// Получение текста письма
static gchar*
get_message_text(EMsgComposer *composer)
{
    if (!composer)
        return NULL;
    
    GString *full_text = g_string_new(NULL);
    
    g_debug("Getting message text from composer");
    
    // Способ 1: Пробуем получить через raw message text
    GByteArray *raw_text = e_msg_composer_get_raw_message_text(composer);
    
    if (raw_text && raw_text->len > 0) {
        g_debug("Got raw message text, length: %u", raw_text->len);
        g_string_append_len(full_text, (gchar *)raw_text->data, raw_text->len);
        g_byte_array_free(raw_text, TRUE);
    } else {
        if (raw_text) g_byte_array_free(raw_text, TRUE);
        
        // Способ 2: Пробуем получить через Camel MIME сообщение
        CamelMimeMessage *message = NULL;
        g_object_get(composer, "message", &message, NULL);
        
        if (message) {
            g_debug("Got Camel message");
            gchar *msg_text = extract_text_from_camel_part(CAMEL_MIME_PART(message));
            if (msg_text) {
                g_debug("Extracted text from Camel message, length: %lu", strlen(msg_text));
                g_string_append(full_text, msg_text);
                g_free(msg_text);
            }
            g_object_unref(message);
        }
        
        // Способ 3: Пробуем получить через свойства composer
        if (full_text->len == 0) {
            gchar *text = NULL;
            g_object_get(composer, "text", &text, NULL);
            if (text && *text) {
                g_debug("Got text from composer property, length: %lu", strlen(text));
                g_string_append(full_text, text);
                g_free(text);
            }
        }
    }
    
    // Если текст не найден, возвращаем NULL
    if (full_text->len == 0) {
        g_debug("No text found in message");
        g_string_free(full_text, TRUE);
        return g_strdup(""); // Пустая строка вместо NULL
    }
    
    g_debug("Final message text length: %lu", full_text->len);
    return g_string_free(full_text, FALSE);
}

// Упрощенная версия для тестирования
static gchar*
get_message_text_simple(EMsgComposer *composer)
{
    gchar *text = get_message_text(composer);
    
    // Для тестирования, если текст не найден, возвращаем тестовую строку
    if (!text || strlen(text) == 0) {
        if (text) g_free(text);
        text = g_strdup("Test message with confidential information in body");
    }
    
    return text;
}

// Основная функция плагина
void
org_gnome_evolution_attachment_checker(EPlugin *ep, gpointer t)
{
    EMEventTargetComposer *target = (EMEventTargetComposer *)t;
    
    // Проверка валидности target
    if (!target || !target->composer) {
        g_warning("Invalid target in attachment checker");
        return;
    }
    
    GSettings *settings = NULL;
    gchar **forbidden_words = NULL;
    gboolean check_attachments = TRUE;
    gboolean check_message_body = TRUE;
    gboolean case_sensitive = FALSE;
    gchar *found_item = NULL;
    gboolean should_cancel = FALSE;
    
    // Загружаем настройки
    settings = g_settings_new(ATTACHMENT_CHECKER_SCHEMA_ID);
    if (!settings) {
        g_warning("Failed to load attachment checker settings");
        return;
    }
    
    check_attachments = g_settings_get_boolean(settings, KEY_CHECK_ATTACHMENTS);
    check_message_body = g_settings_get_boolean(settings, KEY_CHECK_MESSAGE_BODY);
    case_sensitive = g_settings_get_boolean(settings, KEY_CASE_SENSITIVE);
    forbidden_words = load_forbidden_words(settings);
    
    if (!forbidden_words || !forbidden_words[0]) {
        g_strfreev(forbidden_words);
        g_object_unref(settings);
        return;
    }
    
    // Проверяем вложения если включено
    if (check_attachments) {
        EAttachmentView *view = e_msg_composer_get_attachment_view(target->composer);
        if (view) {
            EAttachmentStore *store = e_attachment_view_get_store(view);
            if (store && e_attachment_store_get_num_attachments(store) > 0) {
                if (check_attachment_names(store, forbidden_words, case_sensitive, &found_item)) {
                    should_cancel = TRUE;
                }
            }
        }
    }
    
    // Проверяем текст письма если включено и ещё не нашли нарушений
    if (!should_cancel && check_message_body) {
        gchar *message_text = get_message_text_simple(target->composer);
        if (message_text && strlen(message_text) > 0) {
            if (check_text_for_forbidden_words(message_text, forbidden_words, 
                                                case_sensitive, &found_item)) {
                should_cancel = TRUE;
            }
            g_free(message_text);
        }
    }
    
    // Если найдены нарушения, показываем предупреждение
    if (should_cancel && found_item) {
        GtkWidget *dialog;
        gint response;
        
        gchar *message = g_strdup_printf(
            "Обнаружено запрещённое слово: '%s'\n\n"
            "Вы уверены, что хотите отправить это письмо?",
            found_item
        );
        
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(target->composer),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO,
            "%s", message
        );
        gtk_window_set_title(GTK_WINDOW(dialog), "Проверка безопасности");
        
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(message);
        
        // Если пользователь нажал "Нет" - отменяем отправку
        if (response != GTK_RESPONSE_YES) {
            g_object_set_data(
                G_OBJECT(target->composer),
                "presend_check_status",
                GINT_TO_POINTER(1)
            );
        }
        
        g_free(found_item);
    }
    
    g_strfreev(forbidden_words);
    g_object_unref(settings);
    
    (void)ep;
}
// Функции для UI настроек (оставляем без изменений)
static void
commit_changes(UIData *ui)
{
    if (!ui || !ui->store)
        return;

    GtkTreeModel *model = GTK_TREE_MODEL(ui->store);
    GtkTreeIter iter;
    gboolean valid;
    GPtrArray *words_array;

    words_array = g_ptr_array_new_with_free_func(g_free);

    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *keyword;
        gtk_tree_model_get(model, &iter, WORD_KEYWORD_COLUMN, &keyword, -1);

        if (keyword && strlen(g_strstrip(keyword)) > 0) {
            g_ptr_array_add(words_array, g_strdup(g_strstrip(keyword)));
        }
        g_free(keyword);

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    g_ptr_array_add(words_array, NULL);
    gchar **words = (gchar **)g_ptr_array_free(words_array, FALSE);

    save_forbidden_words(ui->settings, words);
    g_strfreev(words);

    // Сохраняем настройки проверок
    g_settings_set_boolean(ui->settings, KEY_CHECK_ATTACHMENTS,
                           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->check_attachments)));
    g_settings_set_boolean(ui->settings, KEY_CHECK_MESSAGE_BODY,
                           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->check_message_body)));
    g_settings_set_boolean(ui->settings, KEY_CASE_SENSITIVE,
                           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->check_case_sensitive)));
    g_settings_sync();
}

static void
cell_edited_cb(GtkCellRendererText *cell, gchar *path_string,
               gchar *new_text, UIData *ui)
{
    GtkTreeModel *model = GTK_TREE_MODEL(ui->store);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_from_string(model, &iter, path_string)) {
        if (new_text == NULL || *g_strstrip(new_text) == '\0') {
            gtk_list_store_remove(ui->store, &iter);
        } else {
            gtk_list_store_set(ui->store, &iter,
                               WORD_KEYWORD_COLUMN, new_text, -1);
        }

        commit_changes(ui);
    }

    (void)cell;
}

static void
word_add_clicked(GtkButton *button, UIData *ui)
{
    GtkTreeIter iter;

    gtk_list_store_append(ui->store, &iter);
    gtk_list_store_set(ui->store, &iter, WORD_KEYWORD_COLUMN, "", -1);

    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(ui->store), &iter);
    if (path) {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(
            GTK_TREE_VIEW(ui->treeview), WORD_KEYWORD_COLUMN);
        if (column) {
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(ui->treeview),
                                     path, column, TRUE);
        }
        gtk_tree_path_free(path);
    }

    (void)button;
}

static void
word_remove_clicked(GtkButton *button, UIData *ui)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui->treeview));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_list_store_remove(ui->store, &iter);
        commit_changes(ui);
    }

    (void)button;
}

static void
selection_changed(GtkTreeSelection *selection, UIData *ui)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    gboolean has_selection = gtk_tree_selection_get_selected(selection, &model, &iter);
    gtk_widget_set_sensitive(ui->word_remove, has_selection);
}

static void
setting_toggled(GtkToggleButton *button, UIData *ui)
{
    commit_changes(ui);
    (void)button;
}

static void
destroy_ui_data(gpointer data)
{
    UIData *ui = (UIData *)data;

    if (!ui)
        return;

    if (ui->settings)
        g_object_unref(ui->settings);
    g_free(ui);
}

// Функция создания виджета настроек
GtkWidget *
e_plugin_lib_get_configure_widget(EPlugin *plugin)
{
    UIData *ui = g_new0(UIData, 1);

    ui->settings = g_settings_new(ATTACHMENT_CHECKER_SCHEMA_ID);

    // Создаём основной контейнер
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 12);

    // Настройки проверок
    GtkWidget *check_frame = gtk_frame_new(_("Что проверять"));
    GtkWidget *check_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(check_box), 6);
    gtk_container_add(GTK_CONTAINER(check_frame), check_box);
    gtk_box_pack_start(GTK_BOX(main_box), check_frame, FALSE, FALSE, 0);

    ui->check_attachments = gtk_check_button_new_with_label(
        _("Проверять имена вложений"));
    ui->check_message_body = gtk_check_button_new_with_label(
        _("Проверять текст письма"));
    ui->check_case_sensitive = gtk_check_button_new_with_label(
        _("Учитывать регистр"));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->check_attachments),
                                 g_settings_get_boolean(ui->settings, KEY_CHECK_ATTACHMENTS));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->check_message_body),
                                 g_settings_get_boolean(ui->settings, KEY_CHECK_MESSAGE_BODY));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->check_case_sensitive),
                                 g_settings_get_boolean(ui->settings, KEY_CASE_SENSITIVE));

    g_signal_connect(ui->check_attachments, "toggled",
                     G_CALLBACK(setting_toggled), ui);
    g_signal_connect(ui->check_message_body, "toggled",
                     G_CALLBACK(setting_toggled), ui);
    g_signal_connect(ui->check_case_sensitive, "toggled",
                     G_CALLBACK(setting_toggled), ui);

    gtk_box_pack_start(GTK_BOX(check_box), ui->check_attachments, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(check_box), ui->check_message_body, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(check_box), ui->check_case_sensitive, FALSE, FALSE, 0);

    // Список запрещённых слов
    GtkWidget *words_frame = gtk_frame_new(_("Запрещённые слова"));
    GtkWidget *words_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(words_box), 6);
    gtk_container_add(GTK_CONTAINER(words_frame), words_box);
    gtk_box_pack_start(GTK_BOX(main_box), words_frame, TRUE, TRUE, 0);

    // Пояснение
    GtkWidget *label = gtk_label_new(
        _("Слова, которые не должны присутствовать в письме или именах вложений:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(words_box), label, FALSE, FALSE, 0);

    // Контейнер для списка и кнопок
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(words_box), hbox, TRUE, TRUE, 0);

    // Scrolled window со списком
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, 300, 200);
    gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);

    ui->treeview = gtk_tree_view_new();
    gtk_container_add(GTK_CONTAINER(scrolled), ui->treeview);

    // Кнопки управления списком
    GtkWidget *vbutton_box = gtk_button_box_new(GTK_ORIENTATION_VERTICAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(vbutton_box), GTK_BUTTONBOX_START);
    gtk_box_set_spacing(GTK_BOX(vbutton_box), 6);
    gtk_box_pack_start(GTK_BOX(hbox), vbutton_box, FALSE, FALSE, 0);

    ui->word_add = gtk_button_new_with_label(_("Добавить"));
    ui->word_remove = gtk_button_new_with_label(_("Удалить"));
    gtk_container_add(GTK_CONTAINER(vbutton_box), ui->word_add);
    gtk_container_add(GTK_CONTAINER(vbutton_box), ui->word_remove);

    // Создаём модель для списка
    ui->store = gtk_list_store_new(WORD_N_COLUMNS, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(ui->treeview), GTK_TREE_MODEL(ui->store));

    // Колонка со словами
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(ui->treeview), -1, _("Слова"),
        renderer, "text", WORD_KEYWORD_COLUMN, NULL);
    g_object_set(renderer, "editable", TRUE, NULL);
    g_signal_connect(renderer, "edited", G_CALLBACK(cell_edited_cb), ui);

    // Выделение
    GtkTreeSelection *selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(ui->treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(selection_changed), ui);

    // Сигналы кнопок
    g_signal_connect(ui->word_add, "clicked", G_CALLBACK(word_add_clicked), ui);
    g_signal_connect(ui->word_remove, "clicked", G_CALLBACK(word_remove_clicked), ui);

    // Загружаем существующие слова
    gchar **words = load_forbidden_words(ui->settings);
    if (words) {
        for (int i = 0; words[i]; i++) {
            GtkTreeIter iter;
            gtk_list_store_append(ui->store, &iter);
            gtk_list_store_set(ui->store, &iter,
                               WORD_KEYWORD_COLUMN, words[i], -1);
        }
        g_strfreev(words);
    }

    gtk_widget_set_sensitive(ui->word_remove, FALSE);

    g_object_set_data_full(G_OBJECT(main_box), "ui-data", ui, destroy_ui_data);

    gtk_widget_show_all(main_box);
    return main_box;

    (void)plugin;
}

// Функция инициализации плагина
gint
e_plugin_lib_enable(EPlugin *ep, gint enable)
{
    (void)ep;
    (void)enable;
    return 0;
}

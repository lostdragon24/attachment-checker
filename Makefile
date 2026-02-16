PLUGIN = attachment-checker
VERSION = 1.0
SCHEMA_FILE = org.gnome.evolution.plugin.$(PLUGIN).gschema.xml

# Определяем пути установки
prefix ?= /usr
libdir ?= $(prefix)/lib64
plugindir = $(libdir)/evolution/plugins
eplugdir = $(prefix)/share/evolution/plugins
schemadir = $(prefix)/share/glib-2.0/schemas

# Получаем флаги через pkg-config для всех зависимостей
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0 gio-2.0 gobject-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0 gio-2.0 gobject-2.0)

SOUP_CFLAGS := $(shell pkg-config --cflags libsoup-3.0)
SOUP_LIBS := $(shell pkg-config --libs libsoup-3.0)

XML_CFLAGS := $(shell pkg-config --cflags libxml-2.0)
XML_LIBS := $(shell pkg-config --libs libxml-2.0)

JSON_CFLAGS := $(shell pkg-config --cflags json-glib-1.0)
JSON_LIBS := $(shell pkg-config --libs json-glib-1.0)

SECRET_CFLAGS := $(shell pkg-config --cflags libsecret-1)
SECRET_LIBS := $(shell pkg-config --libs libsecret-1)

# WebKit2
WEBKIT_CFLAGS := -I/usr/include/webkitgtk-4.1
WEBKIT_LIBS := -lwebkit2gtk-4.1

# Пути к Evolution (заголовочные файлы)
EVOLUTION_CFLAGS = \
    -I/usr/include \
    -I/usr/include/evolution-3.0 \
    -I/usr/include/evolution-data-server \
    -I/usr/include/evolution \
    -I/usr/include/evolution/e-util \
    -I/usr/include/evolution/mail \
    -I/usr/include/evolution/composer \
    -I/usr/include/evolution/addressbook \
    -I/usr/include/evolution/calendar \
    -I/usr/include/evolution/shell \
    -I/usr/include/evolution-data-server/camel \
    -I/usr/include/evolution-data-server/libedataserver \
    -I/usr/include/evolution-data-server/libedataserverui

# Пути к библиотекам Evolution (исправлено)
EVOLUTION_LIB_DIRS = \
    -L/usr/lib64 \
    -L/usr/lib64/evolution \
    -L/usr/lib64/evolution-data-server

# Библиотеки Evolution (исправленные имена)
EVOLUTION_LIBS = \
    -Wl,--start-group \
    -lcamel-1.2 \
    -ledataserver-1.2 \
    -ledataserverui-1.2 \
    -lebackend-1.2 \
    -lebook-1.2 \
    -lecal-2.0 \
    -l:libevolution-util.so \
    -l:libevolution-mail.so \
    -l:libevolution-mail-composer.so \
    -l:libevolution-mail-formatter.so \
    -l:libevolution-shell.so \
    -l:libevolution-calendar.so \
    -Wl,--end-group

# Добавляем флаги для поиска библиотек в нестандартных местах
LDFLAGS = -Wl,-rpath=/usr/lib64/evolution -Wl,-rpath=/usr/lib64/evolution-data-server

# Итоговые флаги
CFLAGS = -fPIC -Wall -Wextra -g -DVERSION=\"$(VERSION)\" -DGETTEXT_PACKAGE=\"$(PLUGIN)\" \
         $(GTK_CFLAGS) $(GLIB_CFLAGS) $(SOUP_CFLAGS) $(XML_CFLAGS) $(JSON_CFLAGS) $(SECRET_CFLAGS) \
         $(WEBKIT_CFLAGS) $(EVOLUTION_CFLAGS)
LIBS = $(GTK_LIBS) $(GLIB_LIBS) $(SOUP_LIBS) $(XML_LIBS) $(JSON_LIBS) $(SECRET_LIBS) \
       $(WEBKIT_LIBS) $(EVOLUTION_LIB_DIRS) $(EVOLUTION_LIBS) $(LDFLAGS)

# Исходные файлы
SOURCES = $(PLUGIN).c
HEADERS = $(PLUGIN).h
OBJECTS = $(SOURCES:.c=.o)

# Цели
all: info $(PLUGIN).so

info:
	@echo "========================================="
	@echo "Building Attachment Checker plugin"
	@echo "========================================="
	@echo "GTK: $(shell pkg-config --modversion gtk+-3.0 2>/dev/null || echo 'not found')"
	@echo "GLib: $(shell pkg-config --modversion glib-2.0 2>/dev/null || echo 'not found')"
	@echo "libsoup: $(shell pkg-config --modversion libsoup-3.0 2>/dev/null || echo 'not found')"
	@echo "libxml: $(shell pkg-config --modversion libxml-2.0 2>/dev/null || echo 'not found')"
	@echo "json-glib: $(shell pkg-config --modversion json-glib-1.0 2>/dev/null || echo 'not found')"
	@echo "libsecret: $(shell pkg-config --modversion libsecret-1 2>/dev/null || echo 'not found')"
	@echo "========================================="
	@echo ""

$(PLUGIN).so: $(OBJECTS)
	$(CC) -shared -o $@ $^ $(LIBS)
	@echo "Built $(PLUGIN).so"

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

install: install-plugin install-schema install-eplug

install-plugin:
	install -d $(DESTDIR)$(plugindir)
	install -m 755 $(PLUGIN).so $(DESTDIR)$(plugindir)/
	@echo "Plugin installed to $(DESTDIR)$(plugindir)"

install-schema:
	install -d $(DESTDIR)$(schemadir)
	install -m 644 $(SCHEMA_FILE) $(DESTDIR)$(schemadir)/
	@if command -v glib-compile-schemas >/dev/null 2>&1; then \
		glib-compile-schemas $(DESTDIR)$(schemadir); \
		echo "GSchema compiled"; \
	fi
	@echo "GSchema installed to $(DESTDIR)$(schemadir)"

install-eplug:
	install -d $(DESTDIR)$(eplugdir)
	install -m 644 $(PLUGIN).eplug $(DESTDIR)$(eplugdir)/
	@echo "EPlug installed to $(DESTDIR)$(eplugdir)"

clean:
	rm -f $(OBJECTS) $(PLUGIN).so

lib-check:
	@echo "=== Library Search ==="
	@echo "Evolution libraries in /usr/lib64/evolution:"
	@ls -la /usr/lib64/evolution/libevolution* 2>/dev/null || echo "Not found"
	@echo ""
	@echo "Camel libraries:"
	@ls -la /usr/lib64/libcamel* 2>/dev/null || echo "Not found"
	@echo ""
	@echo "EDataServer libraries:"
	@ls -la /usr/lib64/libedata* 2>/dev/null || echo "Not found"
	@echo "=========================="

debug: lib-check
	@echo ""
	@echo "=== COMPILER FLAGS ==="
	@echo "CFLAGS: $(CFLAGS)"
	@echo ""
	@echo "LIBS: $(LIBS)"
	@echo "=========================="

.PHONY: all info install install-plugin install-schema install-eplug clean lib-check debug
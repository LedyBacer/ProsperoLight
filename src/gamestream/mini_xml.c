/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mini_xml.h"
#include "gs_errors.h"

#include <stdlib.h>
#include <string.h>

#define XML_NAME_MAX 128

typedef struct
{
    const char *cursor;
    const char *end;
} xml_scanner_t;

typedef enum
{
    XML_EOF,
    XML_OPEN,
    XML_CLOSE,
    XML_SELF_CLOSE
} xml_token_t;

static void skip_space(xml_scanner_t *scanner)
{
    while (scanner->cursor < scanner->end && (*scanner->cursor == ' ' || *scanner->cursor == '\t' ||
                                              *scanner->cursor == '\r' || *scanner->cursor == '\n'))
        ++scanner->cursor;
}

static char *decode_entities(const char *input, size_t length)
{
    char *output = malloc(length + 1);
    size_t source = 0;
    size_t destination = 0;

    if (!output)
        return NULL;
    while (source < length)
    {
        if (input[source] == '&')
        {
            if (length - source >= 5 && !strncmp(input + source, "&amp;", 5))
            {
                output[destination++] = '&';
                source += 5;
                continue;
            }
            if (length - source >= 4 && !strncmp(input + source, "&lt;", 4))
            {
                output[destination++] = '<';
                source += 4;
                continue;
            }
            if (length - source >= 4 && !strncmp(input + source, "&gt;", 4))
            {
                output[destination++] = '>';
                source += 4;
                continue;
            }
            if (length - source >= 6 && !strncmp(input + source, "&quot;", 6))
            {
                output[destination++] = '"';
                source += 6;
                continue;
            }
            if (length - source >= 6 && !strncmp(input + source, "&apos;", 6))
            {
                output[destination++] = '\'';
                source += 6;
                continue;
            }
        }
        output[destination++] = input[source++];
    }
    output[destination] = '\0';
    return output;
}

static xml_token_t next_tag(xml_scanner_t *scanner, char name[XML_NAME_MAX],
                            const char **attributes, size_t *attributes_length)
{
    for (;;)
    {
        const char *attribute_start;
        size_t name_length = 0;
        int closing = 0;
        int in_quote = 0;
        char quote = 0;
        int self_closing;

        while (scanner->cursor < scanner->end && *scanner->cursor != '<')
            ++scanner->cursor;
        if (scanner->cursor >= scanner->end)
            return XML_EOF;
        ++scanner->cursor;
        if (scanner->cursor >= scanner->end)
            return XML_EOF;
        if (*scanner->cursor == '?' || *scanner->cursor == '!')
        {
            while (scanner->cursor < scanner->end && *scanner->cursor != '>')
                ++scanner->cursor;
            if (scanner->cursor < scanner->end)
                ++scanner->cursor;
            continue;
        }
        if (*scanner->cursor == '/')
        {
            closing = 1;
            ++scanner->cursor;
        }
        while (scanner->cursor < scanner->end && *scanner->cursor != '>' &&
               *scanner->cursor != '/' && *scanner->cursor != ' ' && *scanner->cursor != '\t' &&
               *scanner->cursor != '\r' && *scanner->cursor != '\n')
        {
            if (name_length + 1 < XML_NAME_MAX)
                name[name_length++] = *scanner->cursor;
            ++scanner->cursor;
        }
        name[name_length] = '\0';
        skip_space(scanner);
        attribute_start = scanner->cursor;
        while (scanner->cursor < scanner->end)
        {
            char value = *scanner->cursor;
            if (in_quote)
            {
                if (value == quote)
                    in_quote = 0;
            }
            else if (value == '"' || value == '\'')
            {
                in_quote = 1;
                quote = value;
            }
            else if (value == '>')
            {
                break;
            }
            ++scanner->cursor;
        }
        if (scanner->cursor >= scanner->end)
            return XML_EOF;
        self_closing = scanner->cursor > attribute_start && scanner->cursor[-1] == '/';
        if (attributes)
        {
            *attributes = attribute_start;
            *attributes_length =
                (size_t)(scanner->cursor - attribute_start) - (self_closing ? 1u : 0u);
        }
        ++scanner->cursor;
        if (closing)
            return XML_CLOSE;
        return self_closing ? XML_SELF_CLOSE : XML_OPEN;
    }
}

static void tag_text(const xml_scanner_t *scanner, const char **text, size_t *length)
{
    const char *end = scanner->cursor;
    *text = scanner->cursor;
    while (end < scanner->end && *end != '<')
        ++end;
    *length = (size_t)(end - *text);
}

static int attribute_value(const char *attributes, size_t length, const char *key, char *output,
                           size_t output_size)
{
    const char *cursor = attributes;
    const char *end = attributes + length;
    size_t key_length = strlen(key);

    while (cursor < end)
    {
        const char *name;
        const char *value;
        size_t name_length;
        size_t value_length;
        char quote;

        while (cursor < end &&
               (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n'))
            ++cursor;
        name = cursor;
        while (cursor < end && *cursor != '=' && *cursor != ' ')
            ++cursor;
        name_length = (size_t)(cursor - name);
        while (cursor < end && *cursor != '=')
            ++cursor;
        if (cursor >= end)
            break;
        ++cursor;
        while (cursor < end && *cursor == ' ')
            ++cursor;
        if (cursor >= end || (*cursor != '"' && *cursor != '\''))
            break;
        quote = *cursor++;
        value = cursor;
        while (cursor < end && *cursor != quote)
            ++cursor;
        value_length = (size_t)(cursor - value);
        if (cursor < end)
            ++cursor;
        if (name_length == key_length && !strncmp(name, key, key_length))
        {
            if (value_length >= output_size)
                value_length = output_size - 1;
            memcpy(output, value, value_length);
            output[value_length] = '\0';
            return 0;
        }
    }
    return -1;
}

int xml_status(const char *data, size_t length)
{
    xml_scanner_t scanner = {data, data + length};
    char name[XML_NAME_MAX];
    const char *attributes;
    size_t attributes_length;

    for (;;)
    {
        xml_token_t token = next_tag(&scanner, name, &attributes, &attributes_length);
        if (token == XML_EOF)
            return -1;
        if ((token == XML_OPEN || token == XML_SELF_CLOSE) && !strcmp(name, "root"))
        {
            char value[16];
            if (attribute_value(attributes, attributes_length, "status_code", value,
                                sizeof(value)) == 0)
                return atoi(value);
            return -1;
        }
    }
}

int xml_root_attribute(const char *data, size_t length, const char *attribute, char *output,
                       size_t output_size)
{
    xml_scanner_t scanner;
    char name[XML_NAME_MAX];
    const char *attributes;
    size_t attributes_length;

    if (!data || !attribute || !output || !output_size)
        return GS_INVALID;
    output[0] = '\0';
    scanner.cursor = data;
    scanner.end = data + length;
    for (;;)
    {
        xml_token_t token = next_tag(&scanner, name, &attributes, &attributes_length);
        if (token == XML_EOF)
            return GS_INVALID;
        if ((token == XML_OPEN || token == XML_SELF_CLOSE) && !strcmp(name, "root"))
        {
            char *raw = malloc(output_size);
            char *decoded;
            size_t decoded_length;
            if (!raw)
                return GS_OUT_OF_MEMORY;
            if (attribute_value(attributes, attributes_length, attribute, raw, output_size) != 0)
            {
                free(raw);
                return GS_INVALID;
            }
            decoded = decode_entities(raw, strlen(raw));
            free(raw);
            if (!decoded)
                return GS_OUT_OF_MEMORY;
            decoded_length = strlen(decoded);
            if (decoded_length >= output_size)
                decoded_length = output_size - 1;
            memcpy(output, decoded, decoded_length);
            output[decoded_length] = '\0';
            free(decoded);
            return GS_OK;
        }
    }
}

int xml_search(const char *data, size_t length, const char *node, char **result)
{
    xml_scanner_t scanner = {data, data + length};
    char name[XML_NAME_MAX];

    *result = NULL;
    for (;;)
    {
        xml_token_t token = next_tag(&scanner, name, NULL, NULL);
        if (token == XML_EOF)
            return GS_INVALID;
        if (token == XML_OPEN && !strcmp(name, node))
        {
            const char *text;
            size_t text_length;
            tag_text(&scanner, &text, &text_length);
            *result = decode_entities(text, text_length);
            return *result ? GS_OK : GS_OUT_OF_MEMORY;
        }
        if (token == XML_SELF_CLOSE && !strcmp(name, node))
        {
            *result = calloc(1, 1);
            return *result ? GS_OK : GS_OUT_OF_MEMORY;
        }
    }
}

int xml_applist(const char *data, size_t length, app_entry_t **list)
{
    xml_scanner_t scanner = {data, data + length};
    char name[XML_NAME_MAX];
    app_entry_t *head = NULL;
    app_entry_t *current = NULL;
    app_entry_t *previous = NULL;

    *list = NULL;
    for (;;)
    {
        xml_token_t token = next_tag(&scanner, name, NULL, NULL);
        if (token == XML_EOF)
            break;
        if (token == XML_OPEN && !strcmp(name, "App"))
        {
            current = calloc(1, sizeof(*current));
            if (!current)
                goto out_of_memory;
            current->next = head;
            head = current;
        }
        else if (token == XML_OPEN && current && !strcmp(name, "ID"))
        {
            const char *text;
            size_t text_length;
            tag_text(&scanner, &text, &text_length);
            (void)text_length;
            current->id = atoi(text);
        }
        else if (token == XML_OPEN && current && !strcmp(name, "AppTitle"))
        {
            const char *text;
            size_t text_length;
            tag_text(&scanner, &text, &text_length);
            free(current->name);
            current->name = decode_entities(text, text_length);
            if (!current->name)
                goto out_of_memory;
        }
        else if (token == XML_OPEN && current && !strcmp(name, "IsHdrSupported"))
        {
            const char *text;
            size_t text_length;
            tag_text(&scanner, &text, &text_length);
            (void)text_length;
            current->hdr_supported = atoi(text) == 1;
        }
        else if (token == XML_OPEN && current && !strcmp(name, "IsAppCollectorGame"))
        {
            const char *text;
            size_t text_length;
            tag_text(&scanner, &text, &text_length);
            (void)text_length;
            current->app_collector_game = atoi(text) == 1;
        }
    }
    while (head)
    {
        app_entry_t *next = head->next;
        head->next = previous;
        previous = head;
        head = next;
    }
    *list = previous;
    return GS_OK;

out_of_memory:
    xml_applist_free(head);
    return GS_OUT_OF_MEMORY;
}

void xml_applist_free(app_entry_t *list)
{
    while (list)
    {
        app_entry_t *next = list->next;
        free(list->name);
        free(list);
        list = next;
    }
}

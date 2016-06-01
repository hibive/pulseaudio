/***
  This file is part of PulseAudio.

  Copyright 2016 Arun Raghavan <mail@arunraghavan.net>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include <pulse/json.h>
#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/strbuf.h>

struct pa_json_object {
    PA_REFCNT_DECLARE;
    pa_json_type type;

    union {
        int int_value;
        double double_value;
        bool bool_value;
        char *string_value;
        pa_hashmap *object_values; /* name -> object */
        pa_idxset *array_values; /* objects */
    };
};

#define JSON_OBJECT_TYPE(o) ((o)->type)

static const char* parse_value(const char *str, const char *end, pa_json_object **obj);

static pa_json_object* json_object_new(void) {
    pa_json_object *obj;

    obj = pa_xnew0(pa_json_object, 1);

    return obj;
}

static bool is_whitespace(char c) {
    return c == '\t' || c == '\n' || c == '\r' || c == ' ';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_end(const char c, const char *end) {
    if (!end)
        return c == '\0';
    else  {
        while (*end) {
            if (c == *end)
                return true;
            end++;
        }
    }

    return false;
}

static const char* consume_string(const char *str, const char *expect) {
    while (*expect) {
        if (*str != *expect)
            return NULL;

        str++;
        expect++;
    }

    return str;
}

static const char* parse_null(const char *str, pa_json_object *obj) {
    str = consume_string(str, "null");

    if (str)
        obj->type = PA_JSON_TYPE_NULL;

    return str;
}

static const char* parse_boolean(const char *str, pa_json_object *obj) {
    const char *tmp;

    tmp = consume_string(str, "true");

    if (tmp) {
        obj->type = PA_JSON_TYPE_BOOL;
        obj->bool_value = true;
    } else {
        tmp = consume_string(str, "false");

        if (str) {
            obj->type = PA_JSON_TYPE_BOOL;
            obj->bool_value = false;
        }
    }

    return tmp;
}

static const char* parse_string(const char *str, pa_json_object *obj) {
    pa_strbuf *buf = pa_strbuf_new();

    str++; /* Consume leading '"' */

    while (*str != '"') {
        if (*str != '\\') {
            /* We only accept ASCII printable characters. */
            if (*str < 0x20 || *str > 0x7E) {
                pa_log("Invalid non-ASCII character: 0x%x", (unsigned int) *str);
                goto error;
            }

            /* Normal character, juts consume */
            pa_strbuf_putc(buf, *str);
        } else {
            /* Need to unescape */
            str++;

            switch (*str) {
                case '"':
                case '\\':
                case '/':
                    pa_strbuf_putc(buf, *str);
                    break;

                case 'b':
                    pa_strbuf_putc(buf, '\b' /* backspace */);
                    break;

                case 'f':
                    pa_strbuf_putc(buf, '\f' /* form feed */);
                    break;

                case 'n':
                    pa_strbuf_putc(buf, '\n' /* new line */);
                    break;

                case 'r':
                    pa_strbuf_putc(buf, '\r' /* carriage return */);
                    break;

                case 't':
                    pa_strbuf_putc(buf, '\t' /* horizontal tab */);
                    break;

                case 'u':
                    pa_log("Unicode code points are currently unsupported");
                    goto error;

                default:
                    pa_log("Unexepcted escape value: %c", *str);
                    goto error;
            }
        }

        str++;
    }

    if (*str != '"') {
        pa_log("Failed to parse remainder of string: %s", str);
        goto error;
    }

    str++;

    obj->type = PA_JSON_TYPE_STRING;
    obj->string_value = pa_strbuf_to_string_free(buf);

    return str;

error:
    pa_strbuf_free(buf);
    return NULL;
}

static const char* parse_number(const char *str, pa_json_object *obj) {
    bool negative = false, has_fraction = false, has_exponent = false;
    unsigned int integer = 0;
    unsigned int fraction = 0;
    unsigned int fraction_digits = 0;
    int exponent = 0;

    if (*str == '-') {
        negative = true;
        str++;
    }

    if (*str == '0') {
        str++;
        goto fraction;
    }

    while (is_digit(*str)) {
        integer = (integer * 10) + (*str - '0');
        str++;
    }

fraction:
    if (*str == '.') {
        has_fraction = true;
        str++;

        while (is_digit(*str)) {
            fraction = (fraction * 10) + (*str - '0');
            fraction_digits++;
            str++;
        }
    }

    if (*str == 'e' || *str == 'E') {
        bool exponent_negative = false;

        has_exponent = true;
        str++;

        if (*str == '-') {
            exponent_negative = true;
            str++;
        } else if (*str == '+')
            str++;

        while (is_digit(*str)) {
            exponent = (exponent * 10) + (*str - '0');
            str++;
        }

        if (exponent_negative)
            exponent *= -1;
    }

    if (has_fraction || has_exponent) {
        obj->type = PA_JSON_TYPE_DOUBLE;
        obj->double_value =
            (negative ? -1.0 : 1.0) * (integer + (double) fraction / pow(10, fraction_digits)) * pow(10, exponent);
    } else {
        obj->type = PA_JSON_TYPE_INT;
        obj->int_value = (negative ? -1 : 1) * integer;
    }

    return str;
}

static const char *parse_object(const char *str, pa_json_object *obj) {
    pa_json_object *name = NULL, *value = NULL;

    obj->object_values = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func,
                                             pa_xfree, (pa_free_cb_t) pa_json_object_unref);

    while (*str != '}') {
        str++; /* Consume leading '{' or ',' */

        str = parse_value(str, ":", &name);
        if (!str || JSON_OBJECT_TYPE(name) != PA_JSON_TYPE_STRING) {
            pa_log("Could not parse key for object");
            goto error;
        }

        /* Consume the ':' */
        str++;

        str = parse_value(str, ",}", &value);
        if (!str) {
            pa_log("Could not parse value for object");
            goto error;
        }

        pa_hashmap_put(obj->object_values, pa_xstrdup(pa_json_object_get_string(name)), value);
        pa_json_object_unref(name);

        name = NULL;
        value = NULL;
    }

    /* Drop trailing '}' */
    str++;

    /* We now know the value was correctly parsed */
    obj->type = PA_JSON_TYPE_OBJECT;

    return str;

error:
    pa_hashmap_free(obj->object_values);
    obj->object_values = NULL;

    if (name)
        pa_json_object_unref(name);
    if (value)
        pa_json_object_unref(value);

    return NULL;
}

static const char *parse_array(const char *str, pa_json_object *obj) {
    pa_json_object *value;

    obj->array_values = pa_idxset_new(NULL, NULL);

    while (*str != ']') {
        str++; /* Consume leading '[' or ',' */

        /* Need to chew up whitespaces as a special case to deal with the
         * possibility of an empty array */
        while (is_whitespace(*str))
            str++;

        if (*str == ']')
            break;

        str = parse_value(str, ",]", &value);
        if (!str) {
            pa_log("Could not parse value for array");
            goto error;
        }

        pa_idxset_put(obj->array_values, value, NULL);
    }

    /* Drop trailing ']' */
    str++;

    /* We now know the value was correctly parsed */
    obj->type = PA_JSON_TYPE_ARRAY;

    return str;

error:
    pa_idxset_free(obj->array_values, (pa_free_cb_t) pa_json_object_unref);
    obj->array_values = NULL;
    return NULL;
}

typedef enum {
    JSON_PARSER_STATE_INIT,
    JSON_PARSER_STATE_FINISH,
} json_parser_state;

static const char* parse_value(const char *str, const char *end, pa_json_object **obj) {
    json_parser_state state = JSON_PARSER_STATE_INIT;
    pa_json_object *o;

    pa_assert(str != NULL);

    o = json_object_new();

    while (!is_end(*str, end)) {
        switch (state) {
            case JSON_PARSER_STATE_INIT:
                if (is_whitespace(*str)) {
                    str++;
                } else if (*str == 'n') {
                    str = parse_null(str, o);
                    state = JSON_PARSER_STATE_FINISH;
                } else if (*str == 't' || *str == 'f') {
                    str = parse_boolean(str, o);
                    state = JSON_PARSER_STATE_FINISH;
                } else if (*str == '"') {
                    str = parse_string(str, o);
                    state = JSON_PARSER_STATE_FINISH;
                } else if (is_digit(*str) || *str == '-') {
                    str = parse_number(str, o);
                    state = JSON_PARSER_STATE_FINISH;
                } else if (*str == '{') {
                    str = parse_object(str, o);
                    state = JSON_PARSER_STATE_FINISH;
                } else if (*str == '[') {
                    str = parse_array(str, o);
                    state = JSON_PARSER_STATE_FINISH;
                } else {
                    pa_log("Invalid JSON string: %s", str);
                    goto error;
                }

                if (!str)
                    goto error;

                break;

            case JSON_PARSER_STATE_FINISH:
                /* Consume trailing whitespaces */
                if (is_whitespace(*str)) {
                    str++;
                } else {
                    goto error;
                }
        }
    }

    if (JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_INIT) {
        /* We didn't actually get any data */
        pa_log("No data while parsing json string: '%s' till '%s'", str, pa_strnull(end));
        goto error;
    }

    *obj = o;

    return str;

error:
    pa_json_object_unref(o);
    return NULL;
}


pa_json_object* pa_json_parse(const char *str) {
    pa_json_object *obj;

    str = parse_value(str, NULL, &obj);

    if (!str) {
        pa_log("JSON parsing failed");
        return NULL;
    }

    if (*str != '\0') {
        pa_log("Unable to parse complete JSON string, remainder is: %s", str);
        pa_json_object_unref(obj);
        return NULL;
    }

    return obj;
}

pa_json_type pa_json_object_get_type(const pa_json_object *obj) {
    return JSON_OBJECT_TYPE(obj);
}

void pa_json_object_unref(pa_json_object *obj) {
    if (PA_REFCNT_DEC(obj) > 0)
        return;

    switch (JSON_OBJECT_TYPE(obj)) {
        case PA_JSON_TYPE_INIT:
        case PA_JSON_TYPE_INT:
        case PA_JSON_TYPE_DOUBLE:
        case PA_JSON_TYPE_BOOL:
        case PA_JSON_TYPE_NULL:
            break;

        case PA_JSON_TYPE_STRING:
            pa_xfree(obj->string_value);
            break;

        case PA_JSON_TYPE_OBJECT:
            pa_hashmap_free(obj->object_values);
            break;

        case PA_JSON_TYPE_ARRAY:
            pa_idxset_free(obj->array_values, (pa_free_cb_t) pa_json_object_unref);
            break;

        default:
            pa_assert_not_reached();
    }

    pa_xfree(obj);
}

int pa_json_object_get_int(const pa_json_object *o) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_INT, 0);
    return o->int_value;
}

double pa_json_object_get_double(const pa_json_object *o) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_DOUBLE, 0);
    return o->double_value;
}

bool pa_json_object_get_bool(const pa_json_object *o) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_BOOL, false);
    return o->bool_value;
}

const char* pa_json_object_get_string(const pa_json_object *o) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_STRING, NULL);
    return o->string_value;
}

const pa_json_object* pa_json_object_get_object_member(const pa_json_object *o, const char *name) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_OBJECT, NULL);
    return pa_hashmap_get(o->object_values, name);
}

int pa_json_object_get_array_length(const pa_json_object *o) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_ARRAY, 0);
    return pa_idxset_size(o->array_values);
}

const pa_json_object* pa_json_object_get_array_member(const pa_json_object *o, int index) {
    pa_return_val_if_fail(JSON_OBJECT_TYPE(o) == PA_JSON_TYPE_ARRAY, NULL);
    return pa_idxset_get_by_index(o->array_values, index);
}

/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#include "cli-command.h"
#include "module.h"
#include "sink.h"
#include "source.h"
#include "client.h"
#include "sink-input.h"
#include "source-output.h"
#include "tokenizer.h"
#include "strbuf.h"
#include "namereg.h"
#include "cli-text.h"
#include "scache.h"
#include "sample-util.h"
#include "sound-file.h"
#include "play-memchunk.h"
#include "autoload.h"
#include "xmalloc.h"
#include "sound-file-stream.h"

struct command {
    const char *name;
    int (*proc) (struct pa_core *c, struct pa_tokenizer*t, struct pa_strbuf *buf, int *fail, int *verbose);
    const char *help;
    unsigned args;
};

static int pa_cli_command_exit(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_help(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_modules(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_clients(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_sinks(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_sources(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_sink_inputs(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_source_outputs(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_stat(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_info(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_load(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_unload(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_sink_volume(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_sink_input_volume(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_sink_default(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_source_default(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_kill_client(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_kill_sink_input(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_kill_source_output(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_scache_play(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_scache_remove(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_scache_list(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_scache_load(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_play_file(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_autoload_list(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_autoload_add(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);
static int pa_cli_command_autoload_remove(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose);

static const struct command commands[] = {
    { "exit",                    pa_cli_command_exit,               "Terminate the daemon",         1 },
    { "help",                    pa_cli_command_help,               "Show this help",               1 },
    { "modules",                 pa_cli_command_modules,            "List loaded modules",          1 },
    { "sinks",                   pa_cli_command_sinks,              "List loaded sinks",            1 },
    { "sources",                 pa_cli_command_sources,            "List loaded sources",          1 },
    { "clients",                 pa_cli_command_clients,            "List loaded clients",          1 },
    { "sink_inputs",             pa_cli_command_sink_inputs,        "List sink inputs",             1 },
    { "source_outputs",          pa_cli_command_source_outputs,     "List source outputs",          1 },
    { "stat",                    pa_cli_command_stat,               "Show memory block statistics", 1 },
    { "info",                    pa_cli_command_info,               "Show comprehensive status",    1 },
    { "ls",                      pa_cli_command_info,               NULL,                           1 },
    { "list",                    pa_cli_command_info,               NULL,                           1 },
    { "load",                    pa_cli_command_load,               "Load a module (args: name, arguments)",                     3},
    { "unload",                  pa_cli_command_unload,             "Unload a module (args: index)",                             2},
    { "sink_volume",             pa_cli_command_sink_volume,        "Set the volume of a sink (args: index|name, volume)",             3},
    { "sink_input_volume",       pa_cli_command_sink_input_volume,  "Set the volume of a sink input (args: index|name, volume)", 3},
    { "sink_default",            pa_cli_command_sink_default,       "Set the default sink (args: index|name)", 2},
    { "source_default",          pa_cli_command_source_default,     "Set the default source (args: index|name)", 2},
    { "kill_client",             pa_cli_command_kill_client,        "Kill a client (args: index)", 2},
    { "kill_sink_input",         pa_cli_command_kill_sink_input,    "Kill a sink input (args: index)", 2},
    { "kill_source_output",      pa_cli_command_kill_source_output, "Kill a source output (args: index)", 2},
    { "scache_list",             pa_cli_command_scache_list,        "List all entries in the sample cache", 1},
    { "scache_play",             pa_cli_command_scache_play,        "Play a sample from the sample cache (args: name, sink|index)", 3},
    { "scache_remove",           pa_cli_command_scache_remove,      "Remove a sample from the sample cache (args: name)", 2},
    { "scache_load",             pa_cli_command_scache_load,        "Load a sound file into the sample cache (args: filename,name)", 3},
    { "play_file",               pa_cli_command_play_file,          "Play a sound file (args: filename, sink|index)", 3},
    { "autoload_list",           pa_cli_command_autoload_list,      "List autoload entries", 1},
    { "autoload_sink_add",       pa_cli_command_autoload_add,       "Add autoload entry for a sink (args: sink, name, arguments)", 4},
    { "autoload_source_add",     pa_cli_command_autoload_add,       "Add autoload entry for a source (args: source, name, arguments)", 4},
    { "autoload_sink_remove",    pa_cli_command_autoload_remove,    "Remove autoload entry for a sink (args: sink)", 2},
    { "autoload_source_remove",  pa_cli_command_autoload_remove,    "Remove autoload entry for a source (args: source)", 2},
    { NULL, NULL, NULL, 0 }
};

static const char whitespace[] = " \t\n\r";
static const char linebreak[] = "\n\r";

static uint32_t parse_index(const char *n) {
    long index;
    char *x;
    index = strtol(n, &x, 0);
    if (!x || *x != 0 || index < 0)
        return (uint32_t) PA_IDXSET_INVALID;

    return (uint32_t) index;
}

static int pa_cli_command_exit(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    assert(c && c->mainloop && t);
    c->mainloop->quit(c->mainloop, 0);
    return 0;
}

static int pa_cli_command_help(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const struct command*command;
    assert(c && t && buf);

    pa_strbuf_puts(buf, "Available commands:\n");
    
    for (command = commands; command->name; command++)
        if (command->help)
            pa_strbuf_printf(buf, "    %-20s %s\n", command->name, command->help);
    return 0;
}

static int pa_cli_command_modules(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_module_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_clients(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_client_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_sinks(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_sink_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_sources(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_source_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_sink_inputs(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_sink_input_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_source_outputs(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_source_output_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_stat(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    assert(c && t);
    pa_strbuf_printf(buf, "Memory blocks currently allocated: %u, size: %u bytes.\nMemory blocks allocated during the whole lifetime: %u, size: %u bytes.\n", c->memblock_stat->total, c->memblock_stat->total_size, c->memblock_stat->allocated, c->memblock_stat->allocated_size);
    return 0;
}

static int pa_cli_command_info(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    assert(c && t);
    pa_cli_command_stat(c, t, buf, fail, verbose);
    pa_cli_command_modules(c, t, buf, fail, verbose);
    pa_cli_command_sinks(c, t, buf, fail, verbose);
    pa_cli_command_sources(c, t, buf, fail, verbose);
    pa_cli_command_clients(c, t, buf, fail, verbose);
    pa_cli_command_sink_inputs(c, t, buf, fail, verbose);
    pa_cli_command_source_outputs(c, t, buf, fail, verbose);
    pa_cli_command_scache_list(c, t, buf, fail, verbose);
    pa_cli_command_autoload_list(c, t, buf, fail, verbose);
    return 0;
}

static int pa_cli_command_load(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    struct pa_module *m;
    const char *name;
    char txt[256];
    assert(c && t);

    if (!(name = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify the module name and optionally arguments.\n");
        return -1;
    }
    
    if (!(m = pa_module_load(c, name,  pa_tokenizer_get(t, 2)))) {
        pa_strbuf_puts(buf, "Module load failed.\n");
        return -1;
    }

    if (*verbose) {
        snprintf(txt, sizeof(txt), "Module successfully loaded, index: %u.\n", m->index);
        pa_strbuf_puts(buf, txt);
    }
    return 0;
}

static int pa_cli_command_unload(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    struct pa_module *m;
    uint32_t index;
    const char *i;
    char *e;
    assert(c && t);

    if (!(i = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify the module index.\n");
        return -1;
    }

    index = (uint32_t) strtoul(i, &e, 10);
    if (*e || !(m = pa_idxset_get_by_index(c->modules, index))) {
        pa_strbuf_puts(buf, "Invalid module index.\n");
        return -1;
    }

    pa_module_unload_request(c, m);
    return 0;
}

static int pa_cli_command_sink_volume(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n, *v;
    char *x = NULL;
    struct pa_sink *sink;
    long volume;

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a sink either by its name or its index.\n");
        return -1;
    }

    if (!(v = pa_tokenizer_get(t, 2))) {
        pa_strbuf_puts(buf, "You need to specify a volume >= 0. (0 is muted, 0x100 is normal volume)\n");
        return -1;
    }

    volume = strtol(v, &x, 0);
    if (!x || *x != 0 || volume < 0) {
        pa_strbuf_puts(buf, "Failed to parse volume.\n");
        return -1;
    }

    if (!(sink = pa_namereg_get(c, n, PA_NAMEREG_SINK, 1))) {
        pa_strbuf_puts(buf, "No sink found by this name or index.\n");
        return -1;
    }

    pa_sink_set_volume(sink, (uint32_t) volume);
    return 0;
}

static int pa_cli_command_sink_input_volume(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n, *v;
    struct pa_sink_input *si;
    long volume;
    uint32_t index;
    char *x;

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a sink input by its index.\n");
        return -1;
    }

    if ((index = parse_index(n)) == PA_IDXSET_INVALID) {
        pa_strbuf_puts(buf, "Failed to parse index.\n");
        return -1;
    }

    if (!(v = pa_tokenizer_get(t, 2))) {
        pa_strbuf_puts(buf, "You need to specify a volume >= 0. (0 is muted, 0x100 is normal volume)\n");
        return -1;
    }

    x = NULL;
    volume = strtol(v, &x, 0);
    if (!x || *x != 0 || volume < 0) {
        pa_strbuf_puts(buf, "Failed to parse volume.\n");
        return -1;
    }

    if (!(si = pa_idxset_get_by_index(c->sink_inputs, (uint32_t) index))) {
        pa_strbuf_puts(buf, "No sink input found with this index.\n");
        return -1;
    }

    pa_sink_input_set_volume(si, (uint32_t) volume);
    return 0;
}

static int pa_cli_command_sink_default(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n;
    assert(c && t);

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a sink either by its name or its index.\n");
        return -1;
    }

    pa_namereg_set_default(c, n, PA_NAMEREG_SINK);
    return 0;
}

static int pa_cli_command_source_default(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n;
    assert(c && t);

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a source either by its name or its index.\n");
        return -1;
    }

    pa_namereg_set_default(c, n, PA_NAMEREG_SOURCE);
    return 0;
}

static int pa_cli_command_kill_client(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n;
    struct pa_client *client;
    uint32_t index;
    assert(c && t);

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a client by its index.\n");
        return -1;
    }

    if ((index = parse_index(n)) == PA_IDXSET_INVALID) {
        pa_strbuf_puts(buf, "Failed to parse index.\n");
        return -1;
    }

    if (!(client = pa_idxset_get_by_index(c->clients, index))) {
        pa_strbuf_puts(buf, "No client found by this index.\n");
        return -1;
    }

    pa_client_kill(client);
    return 0;
}

static int pa_cli_command_kill_sink_input(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n;
    struct pa_sink_input *sink_input;
    uint32_t index;
    assert(c && t);

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a sink input by its index.\n");
        return -1;
    }

    if ((index = parse_index(n)) == PA_IDXSET_INVALID) {
        pa_strbuf_puts(buf, "Failed to parse index.\n");
        return -1;
    }

    if (!(sink_input = pa_idxset_get_by_index(c->sink_inputs, index))) {
        pa_strbuf_puts(buf, "No sink input found by this index.\n");
        return -1;
    }

    pa_sink_input_kill(sink_input);
    return 0;
}

static int pa_cli_command_kill_source_output(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n;
    struct pa_source_output *source_output;
    uint32_t index;
    assert(c && t);

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a source output by its index.\n");
        return -1;
    }

    if ((index = parse_index(n)) == PA_IDXSET_INVALID) {
        pa_strbuf_puts(buf, "Failed to parse index.\n");
        return -1;
    }

    if (!(source_output = pa_idxset_get_by_index(c->source_outputs, index))) {
        pa_strbuf_puts(buf, "No source output found by this index.\n");
        return -1;
    }

    pa_source_output_kill(source_output);
    return 0;
}

static int pa_cli_command_scache_list(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_scache_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

static int pa_cli_command_scache_play(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n, *sink_name;
    struct pa_sink *sink;
    assert(c && t && buf && fail && verbose);

    if (!(n = pa_tokenizer_get(t, 1)) || !(sink_name = pa_tokenizer_get(t, 2))) {
        pa_strbuf_puts(buf, "You need to specify a sample name and a sink name.\n");
        return -1;
    }

    if (!(sink = pa_namereg_get(c, sink_name, PA_NAMEREG_SINK, 1))) {
        pa_strbuf_puts(buf, "No sink by that name.\n");
        return -1;
    }

    if (pa_scache_play_item(c, n, sink, PA_VOLUME_NORM) < 0) {
        pa_strbuf_puts(buf, "Failed to play sample.\n");
        return -1;
    }

    return 0;
}

static int pa_cli_command_scache_remove(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *n;
    assert(c && t && buf && fail && verbose);

    if (!(n = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a sample name.\n");
        return -1;
    }

    if (pa_scache_remove_item(c, n) < 0) {
        pa_strbuf_puts(buf, "Failed to remove sample.\n");
        return -1;
    }

    return 0;
}

static int pa_cli_command_scache_load(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *fname, *n;
    struct pa_memchunk chunk;
    struct pa_sample_spec ss;
    assert(c && t && buf && fail && verbose);

    if (!(fname = pa_tokenizer_get(t, 1)) || !(n = pa_tokenizer_get(t, 2))) {
        pa_strbuf_puts(buf, "You need to specify a file name and a sample name.\n");
        return -1;
    }

    if (pa_sound_file_load(fname, &ss, &chunk, c->memblock_stat) < 0) {
        pa_strbuf_puts(buf, "Failed to load sound file.\n");
        return -1;
    }

    pa_scache_add_item(c, n, &ss, &chunk, NULL);
    pa_memblock_unref(chunk.memblock);
    return 0;
}

static int pa_cli_command_play_file(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *fname, *sink_name;
    struct pa_sink *sink;
    assert(c && t && buf && fail && verbose);

    if (!(fname = pa_tokenizer_get(t, 1)) || !(sink_name = pa_tokenizer_get(t, 2))) {
        pa_strbuf_puts(buf, "You need to specify a file name and a sink name.\n");
        return -1;
    }

    if (!(sink = pa_namereg_get(c, sink_name, PA_NAMEREG_SINK, 1))) {
        pa_strbuf_puts(buf, "No sink by that name.\n");
        return -1;
    }


    return pa_play_file(sink, fname, PA_VOLUME_NORM);
}

static int pa_cli_command_autoload_add(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *devname, *module;
    assert(c && t && buf && fail && verbose);

    if (!(devname = pa_tokenizer_get(t, 1)) || !(module = pa_tokenizer_get(t, 2))) {
        pa_strbuf_puts(buf, "You need to specify a device name, a module name and optionally module arguments\n");
        return -1;
    }
    
    pa_autoload_add(c, devname, strstr(pa_tokenizer_get(t, 0), "sink") ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE, module, pa_tokenizer_get(t, 3));
    return 0;
}

static int pa_cli_command_autoload_remove(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *devname;
    assert(c && t && buf && fail && verbose);

    if (!(devname = pa_tokenizer_get(t, 1))) {
        pa_strbuf_puts(buf, "You need to specify a device name\n");
        return -1;
    }

    if (pa_autoload_remove(c, devname, strstr(pa_tokenizer_get(t, 0), "sink") ? PA_NAMEREG_SINK : PA_NAMEREG_SOURCE) < 0) {
        pa_strbuf_puts(buf, "Failed to remove autload entry\n");
        return -1;
    }

    return 0;        
}

static int pa_cli_command_autoload_list(struct pa_core *c, struct pa_tokenizer *t, struct pa_strbuf *buf, int *fail, int *verbose) {
    char *s;
    assert(c && t);
    s = pa_autoload_list_to_string(c);
    assert(s);
    pa_strbuf_puts(buf, s);
    pa_xfree(s);
    return 0;
}

int pa_cli_command_execute_line(struct pa_core *c, const char *s, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *cs;
    
    cs = s+strspn(s, whitespace);

    if (*cs == '#' || !*cs)
        return 0;
    else if (*cs == '.') {
        static const char fail_meta[] = ".fail";
        static const char nofail_meta[] = ".nofail";
        static const char verbose_meta[] = ".verbose";
        static const char noverbose_meta[] = ".noverbose";

        if (!strcmp(cs, verbose_meta))
            *verbose = 1;
        else if (!strcmp(cs, noverbose_meta))
            *verbose = 0;
        else if (!strcmp(cs, fail_meta))
            *fail = 1;
        else if (!strcmp(cs, nofail_meta))
            *fail = 0;
        else {
            size_t l;
            static const char include_meta[] = ".include";
            l = strcspn(cs, whitespace);

            if (l == sizeof(include_meta)-1 && !strncmp(cs, include_meta, l)) {
                const char *filename = cs+l+strspn(cs+l, whitespace);

                if (pa_cli_command_execute_file(c, filename, buf, fail, verbose) < 0)
                    if (*fail) return -1;
            } else {
                pa_strbuf_printf(buf, "Invalid meta command: %s\n", cs);
                if (*fail) return -1;
            }
        }
    } else {
        const struct command*command;
        int unknown = 1;
        size_t l;
        
        l = strcspn(cs, whitespace);

        for (command = commands; command->name; command++) 
            if (strlen(command->name) == l && !strncmp(cs, command->name, l)) {
                int ret;
                struct pa_tokenizer *t = pa_tokenizer_new(cs, command->args);
                assert(t);
                ret = command->proc(c, t, buf, fail, verbose);
                pa_tokenizer_free(t);
                unknown = 0;

                if (ret < 0 && *fail)
                    return -1;
                
                break;
            }

        if (unknown) {
            pa_strbuf_printf(buf, "Unknown command: %s\n", cs);
            if (*fail)
                return -1;
        }
    }

    return 0;
}

int pa_cli_command_execute_file(struct pa_core *c, const char *fn, struct pa_strbuf *buf, int *fail, int *verbose) {
    char line[256];
    FILE *f = NULL;
    int ret = -1;
    assert(c && fn && buf);

    if (!(f = fopen(fn, "r"))) {
        pa_strbuf_printf(buf, "open('%s') failed: %s\n", fn, strerror(errno));
        if (!*fail)
            ret = 0;
        goto fail;
    }

    if (*verbose)
        pa_strbuf_printf(buf, "Executing file: '%s'\n", fn);

    while (fgets(line, sizeof(line), f)) {
        char *e = line + strcspn(line, linebreak);
        *e = 0;

        if (pa_cli_command_execute_line(c, line, buf, fail, verbose) < 0 && *fail)
            goto fail;
    }

    if (*verbose)
        pa_strbuf_printf(buf, "Executed file: '%s'\n", fn);

    ret = 0;

fail:
    if (f)
        fclose(f);

    return ret;
}

int pa_cli_command_execute(struct pa_core *c, const char *s, struct pa_strbuf *buf, int *fail, int *verbose) {
    const char *p;
    assert(c && s && buf && fail && verbose);

    p = s;
    while (*p) {
        size_t l = strcspn(p, linebreak);
        char *line = pa_xstrndup(p, l);
        
        if (pa_cli_command_execute_line(c, line, buf, fail, verbose) < 0&& *fail) {
            pa_xfree(line);
            return -1;
        }
        pa_xfree(line);

        p += l;
        p += strspn(p, linebreak);
    }

    return 0;
}

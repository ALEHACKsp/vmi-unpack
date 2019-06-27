/*
 * Copyright (c) 2017 Carter Yagemann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>

#include <libvmi/libvmi.h>

#include <monitor.h>
#include <dump.h>
#include <output.h>
#include <paging/intel_64.h>
#include <vmi/process.h>

void process_layer(vmi_instance_t vmi, vmi_event_t *event, vmi_pid_t pid, page_cat_t page_cat)
{
    size_t dump_size;

    mem_seg_t vma = vmi_current_find_segment(vmi, event, event->mem_event.gla);
    if (!vma.size)
    {
        fprintf(stderr, "WARNING: Unpack - Could not find memory segment for virtual address 0x%lx\n", event->mem_event.gla);
        return;
    }

    char *buffer = (char *) malloc(vma.size);
    if (!buffer)
    {
        fprintf(stderr, "ERROR: Unpack - Failed to malloc buffer to dump W2X event\n");
        return;
    }

    vmi_read_va(vmi, vma.base_va, pid, vma.size, buffer, &dump_size);
    printf("Dumping section: base_va: %p, size: %zu\n", (void *)(vma.base_va), vma.size);
    add_to_dump_queue(buffer, dump_size, pid, event->x86_regs->rip, vma.base_va);
    printf("Done queueing dump: base_va: %p, size: %zu\n", (void *)(vma.base_va), vma.size);
}

uint64_t extract_field_value(uint64_t packed, uint64_t start, uint64_t end)
{
    packed >>= start;
    gint64 mask = (1ULL << (end - start + 1)) - 1;
    packed &= mask;
    return packed;
}

void get_node_vadtype(vmi_instance_t vmi, addr_t node, vadtype_t *vadtype)
{
    uint64_t flags = 0;
    uint64_t start = process_vmi_windows_rekall.flags_vadtype_start;
    uint64_t end = process_vmi_windows_rekall.flags_vadtype_end;
    vmi_read_64_va(vmi,
                   node + process_vmi_windows_rekall.mmvad_flags,
                   0, &flags);
    *vadtype = (vadtype_t)extract_field_value(flags, start, end);
}

void get_node_isprivate(vmi_instance_t vmi, addr_t node, uint8_t *isprivate)
{
    uint64_t flags = 0;
    uint64_t start = process_vmi_windows_rekall.flags_isprivate_start;
    uint64_t end = process_vmi_windows_rekall.flags_isprivate_end;
    vmi_read_64_va(vmi,
                   node + process_vmi_windows_rekall.mmvad_flags,
                   0, &flags);
    *isprivate = (uint8_t)extract_field_value(flags, start, end);
}

void get_node_protection(vmi_instance_t vmi, addr_t node, uint8_t *protection)
{
    uint64_t flags = 0;
    uint64_t start = process_vmi_windows_rekall.flags_protection_start;
    uint64_t end = process_vmi_windows_rekall.flags_protection_end;
    vmi_read_64_va(vmi,
                   node + process_vmi_windows_rekall.mmvad_flags,
                   0, &flags);
    *protection = (uint8_t)extract_field_value(flags, start, end);
}

// you must free the returned string:
//   free(fn_utf8.contents);
void get_node_filename(vmi_instance_t vmi, addr_t node, unicode_string_t *fn_utf8)
{
    unicode_string_t *fn_ustr = NULL;
    addr_t control_area = 0, file_object = 0, filename_ptr = 0;
    vmi_read_addr_va(vmi,
                     node + process_vmi_windows_rekall.mmvad_controlarea,
                     0, &control_area);
    if (control_area == 0) return;
    vmi_read_addr_va(vmi,
                     control_area + process_vmi_windows_rekall.controlarea_fileobject,
                     0, &file_object);
    if (file_object == 0) return;
    file_object &= 0xFFFFFFFFFFFFFFF8; //because its an EX_FAST_REF
    vmi_read_addr_va(vmi,
                     file_object + process_vmi_windows_rekall.fileobject_filename,
                     0, &filename_ptr);
    if (filename_ptr == 0) return;
    fn_ustr = vmi_read_unicode_str_va(vmi, filename_ptr, 0 /* pid=0 for kernel struct */);
    if (fn_ustr == NULL) return;
    vmi_convert_str_encoding(fn_ustr, fn_utf8, "UTF-8");
    vmi_free_unicode_str(fn_ustr);
}

void handle_node(vmi_instance_t vmi, addr_t node, void *data)
{
    dump_layer_t *dump = (dump_layer_t *)data;
    if (dump->segment_count >= SEG_COUNT_MAX)
    {
        fprintf(stderr, "handle_node: skipping VAD, SEG_COUNT_MAX reached.\n");
        return;
    }
    addr_t start = 0, end = 0, base_va = 0, size = 0;
    if (vmi_read_addr_va(
            vmi,
            node + process_vmi_windows_rekall.mmvad_startingvpn,
            0,
            &start) != VMI_SUCCESS)
        return;
    start <<= 12;
    if (vmi_read_addr_va(
            vmi,
            node + process_vmi_windows_rekall.mmvad_endingvpn,
            0,
            &end) != VMI_SUCCESS)
        return;
    end <<= 12;
    if (!start || !end) return;
    base_va = start;
    size = end - start;
    dump->segments[dump->segment_count] = malloc(sizeof(vad_seg_t));
    dump->segments[dump->segment_count]->base_va = base_va;
    dump->segments[dump->segment_count]->size = size;
    dump->segments[dump->segment_count]->va_size = size;
    dump->segments[dump->segment_count]->buf = malloc(size);
    size_t read_size = 0;
    vmi_read_va(
        vmi,
        base_va,
        dump->pid,
        size,
        dump->segments[dump->segment_count]->buf, &read_size);
    if (read_size < size)
    {
        dump->segments[dump->segment_count]->buf =
            realloc(dump->segments[dump->segment_count]->buf, read_size);
        dump->segments[dump->segment_count]->size = read_size;
    }
    // filename
    dump->segments[dump->segment_count]->filename = (const unicode_string_t) {0};
    get_node_filename(vmi, node, &(dump->segments[dump->segment_count]->filename));
    // vadtype
    get_node_vadtype(vmi, node, &(dump->segments[dump->segment_count]->vadtype));
    // isprivate
    get_node_isprivate(vmi, node, &(dump->segments[dump->segment_count]->isprivate));
    // protection
    get_node_protection(vmi, node, &(dump->segments[dump->segment_count]->protection));

    dump->segment_count += 1;
}

void vad_dump_process(vmi_instance_t vmi, vmi_event_t *event, vmi_pid_t pid, page_cat_t page_cat)
{
    addr_t eprocess = windows_find_eprocess_pgd(vmi, event->x86_regs->cr3);
    addr_t vadroot = vmi_get_eprocess_vadroot(vmi, eprocess);
    dump_layer_t *dump;
    dump = malloc(sizeof(dump_layer_t));
    dump->pid = pid;
    dump->rip = event->x86_regs->rip;
    dump->segment_count = 0;
    dump->segments = malloc(sizeof(vad_seg_t *) * SEG_COUNT_MAX);
    vad_iterator(vmi, vadroot, handle_node, dump);
    queue_vads_to_dump(dump);
}

void vad_iterator(vmi_instance_t vmi, addr_t node, traverse_func func, void *data)
{
    addr_t left = 0, right = 0;
    if (vmi_read_addr_va(
            vmi,
            node + process_vmi_windows_rekall.mmvad_leftchild,
            0,
            &left) != VMI_SUCCESS)
        fprintf(stderr, "vad_iterator: Left node could not be read\n");
    if (left)
        vad_iterator(vmi, left, func, data);
    func(vmi, node, data);
    if (vmi_read_addr_va(
            vmi,
            node + process_vmi_windows_rekall.mmvad_rightchild,
            0,
            &right) != VMI_SUCCESS)
        fprintf(stderr, "vad_iterator: Right node could not be read\n");
    if (right)
        vad_iterator(vmi, right, func, data);
}
/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

// Macro to advance either in_offs or out_offs and wrap if necessary. Returns
// new value of offset.  Could be in header, but added here to limit changes
// to one file.
#define AESDCHAR_ADVANCE_PTR(x) (((x)+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */
    uint8_t out_offs;

    // Check for NULL inputs, return if so
    if ((!buffer) || (!entry_offset_byte_rtn)) {
	return NULL;
    }

    out_offs = buffer->out_offs;
    while (buffer->entry[out_offs].size <= char_offset)
    {
	char_offset -= buffer->entry[out_offs].size;
	out_offs = AESDCHAR_ADVANCE_PTR(out_offs);
	if (out_offs == buffer->in_offs) {
	    return NULL;
	}
    }

    *entry_offset_byte_rtn = char_offset;
    return &(buffer->entry[out_offs]);
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* Return NULL or, if an existing entry at out_offs was replaced, the value of
* buffptr for the entry which was replaced (for deallocation by the caller)
*/
// Caller is responsible for locking around calls!
const char * aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */

    const char * ret_val = NULL;

    // Check for NULL inputs, return if so
    if ((!buffer) || (!add_entry)) {
	return NULL;
    }

    // Three possible states to handle:
    //  1 - in_offs == out_offs, !full - ring empty, advance in_offs
    //  2 - in_offs == out_offs, full - advance in_offs and out_offs
    //      (discard oldest value) - but how do you deallocate lost buffptr?
    //  3 - in_offs != out_offs, full==<don't care> - advance in_offs and
    //      check for in_offs == out_offs AFTER increment.  Set full if so.
    //
    // If state 2, return buffptr about to be discarded so caller can deallocate
    // advance out_offs, discarding entry.
    // Need to advance both pointers after add.  Do out_offs here,
    // in_offs is always done below.
    if ((buffer->in_offs == buffer->out_offs) && buffer->full) {
	ret_val = buffer->entry[buffer->out_offs].buffptr;
	buffer->out_offs = AESDCHAR_ADVANCE_PTR(buffer->out_offs);
    }

    // First, write the new entry to the array at location in_offs
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size    = add_entry->size;

    // Advance in_off AFTER check above (analyze pre-insert state)
    buffer->in_offs = AESDCHAR_ADVANCE_PTR(buffer->in_offs);

    // Set flag if we just filled the buffer (in wrapped around to meet out)
    // May have already been full, in which case this is a nop
    if (buffer->in_offs == buffer->out_offs) {
	buffer->full = true;
    }
    return ret_val;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

/*
 * Returns total number of bytes in buffer.
 * Any necessary locking must be performed by caller.
 */
loff_t aesd_circular_buffer_size(struct aesd_circular_buffer *buffer)
{
    struct aesd_buffer_entry *entry;
    uint8_t index;
    loff_t retval = 0;

    AESD_CIRCULAR_BUFFER_FOREACH(entry,buffer,index) {
	if (entry->buffptr) {
	    retval += entry->size;
	}
    }
    return retval;
}

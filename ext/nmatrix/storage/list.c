/////////////////////////////////////////////////////////////////////
// = NMatrix
//
// A linear algebra library for scientific computation in Ruby.
// NMatrix is part of SciRuby.
//
// NMatrix was originally inspired by and derived from NArray, by
// Masahiro Tanaka: http://narray.rubyforge.org
//
// == Copyright Information
//
// SciRuby is Copyright (c) 2010 - 2012, Ruby Science Foundation
// NMatrix is Copyright (c) 2012, Ruby Science Foundation
//
// Please see LICENSE.txt for additional copyright notices.
//
// == Contributing
//
// By contributing source code to SciRuby, you agree to be bound by
// our Contributor Agreement:
//
// * https://github.com/SciRuby/sciruby/wiki/Contributor-Agreement
//
// == list.c
//
// List-of-lists n-dimensional matrix storage. Uses singly-linked
// lists.

/*
 * Standard Includes
 */

#include <ruby.h>

/*
 * Project Includes
 */

#include "nmatrix.h"

/*
 * Macros
 */

/*
 * Global Variables
 */

extern						VALUE nm_eStorageTypeError;
extern bool				(*ElemEqEq[NM_TYPES][2])(const void*, const void*, const int, const int);
extern const int	nm_sizeof[NM_TYPES];

/*
 * Forward Declarations
 */

static bool list_storage_cast_copy_contents_dense(LIST* lhs, const char* rhs, void* zero, int8_t l_dtype, int8_t r_dtype, size_t* pos, size_t* coords, const size_t* shape, size_t rank, size_t recursions);

/*
 * Functions
 */

////////////////
// Lifecycle //
///////////////

/*
 * Creates a list-of-lists(-of-lists-of-lists-etc) storage framework for a
 * matrix.
 *
 * Note: The pointers you pass in for shape and init_val become property of our
 * new storage. You don't need to free them, and you shouldn't re-use them.
 */
LIST_STORAGE* list_storage_create(int8_t dtype, size_t* shape, size_t rank, void* init_val) {
  LIST_STORAGE* s;

  s = ALLOC( LIST_STORAGE );

  s->rank  = rank;
  s->shape = shape;
  s->dtype = dtype;

  s->rows  = create_list();

  s->default_val = init_val;

  return s;
}

/*
 * Documentation goes here.
 */
void list_storage_delete(LIST_STORAGE* s) {
  if (s) {
    //fprintf(stderr, "* Deleting list storage rows at %p\n", s->rows);
    delete_list( s->rows, s->rank - 1 );

    //fprintf(stderr, "  Deleting list storage shape at %p\n", s->shape);
    free(s->shape);
    //fprintf(stderr, "  Deleting list storage default_val at %p\n", s->default_val);
    free(s->default_val);
    //fprintf(stderr, "  Deleting list storage at %p\n", s);
    free(s);
  }
}

/*
 * Documentation goes here.
 */
void mark_list_storage(void* m) {
  LIST_STORAGE* storage;

  if (m) {
    storage = (LIST_STORAGE*)(((NMATRIX*)m)->storage);
    
    if (storage && storage->dtype == NM_ROBJ) {
      rb_gc_mark(*((VALUE*)(storage->default_val)));
      list_mark(storage->rows, storage->rank - 1);
    }
  }
}

///////////////
// Accessors //
///////////////

/*
 * Get the contents of some set of coordinates. Note: Does not make a copy!
 * Don't free!
 */
void* list_storage_get(LIST_STORAGE* s, SLICE* slice) {
  //LIST_STORAGE* s = (LIST_STORAGE*)(t);
  size_t r;
  NODE*  n;
  LIST*  l = s->rows;

  for (r = s->rank; r > 1; --r) {
    n = list_find(l, slice->coords[s->rank - r]);
    if (n)  l = n->val;
    else return s->default_val;
  }

  n = list_find(l, slice->coords[s->rank - r]);
  if (n) return n->val;
  else   return s->default_val;
}

/*
 * Documentation goes here.
 *
 * TODO: Allow this function to accept an entire row and not just one value -- for slicing
 */
void* list_storage_insert(LIST_STORAGE* s, SLICE* slice, void* val) {
  // Pretend ranks = 2
  // Then coords is going to be size 2
  // So we need to find out if some key already exists
  size_t r;
  NODE*  n;
  LIST*  l = s->rows;

  // drill down into the structure
  for (r = s->rank; r > 1; --r) {
    n = list_insert(l, false, slice->coords[s->rank - r], create_list());
    l = n->val;
  }

  n = list_insert(l, true, slice->coords[s->rank - r], val);
  return n->val;
}

/*
 * Documentation goes here.
 *
 * TODO: Speed up removal.
 */
void* list_storage_remove(LIST_STORAGE* s, SLICE* slice) {
  int r;
  NODE  *n = NULL;
  LIST*  l = s->rows;
  void*  rm = NULL;

  // keep track of where we are in the traversals
  NODE** stack = ALLOCA_N( NODE*, s->rank - 1 );

  for (r = (int)(s->rank); r > 1; --r) {
  	// does this row exist in the matrix?
    n = list_find(l, slice->coords[s->rank - r]);

    if (!n) {
    	// not found
      free(stack);
      return NULL;
      
    } else {
    	// found
      stack[s->rank - r]    = n;
      l                     = n->val;
    }
  }

  rm = list_remove(l, slice->coords[s->rank - r]);

  // if we removed something, we may now need to remove parent lists
  if (rm) {
    for (r = (int)(s->rank) - 2; r >= 0; --r) {
    	// walk back down the stack
      
      if (((LIST*)(stack[r]->val))->first == NULL) {
        free(list_remove(stack[r]->val, slice->coords[r]));
        
      } else {
      	// no need to continue unless we just deleted one.
        break;
      }
    }
  }

  return rm;
}

///////////
// Tests //
///////////

/*
 * Do these two dense matrices of the same dtype have exactly the same
 * contents?
 *
 * FIXME: Add templating.
 */
bool list_storage_eqeq(const LIST_STORAGE* left, const LIST_STORAGE* right) {

  // in certain cases, we need to keep track of the number of elements checked.
  size_t num_checked  = 0,
         max_elements = count_storage_max_elements((STORAGE*)left);
  
  (*eqeq)(const void*, const void*, const int, const int) = ElemEqEq[left->dtype][0];

  if (!left->rows->first) {
    // fprintf(stderr, "!left->rows true\n");
    // Easy: both lists empty -- just compare default values
    if (!right->rows->first) {
    	return eqeq(left->default_val, right->default_val, 1, nm_sizeof[left->dtype]);
    	
    } else if (!list_eqeq_value(right->rows, left->default_val, left->dtype, left->rank-1, &num_checked)) {
    	// Left empty, right not empty. Do all values in right == left->default_val?
    	return false;
    	
    } else if (num_checked < max_elements) {
    	// If the matrix isn't full, we also need to compare default values.
    	return eqeq(left->default_val, right->default_val, 1, nm_sizeof[left->dtype]);
    }

  } else if (!right->rows->first) {
    // fprintf(stderr, "!right->rows true\n");
    // Right empty, left not empty. Do all values in left == right->default_val?
    if (!list_eqeq_value(left->rows, right->default_val, left->dtype, left->rank-1, &num_checked)) {
    	return false;
    	
    } else if (num_checked < max_elements) {
   		// If the matrix isn't full, we also need to compare default values.
    	return eqeq(left->default_val, right->default_val, 1, nm_sizeof[left->dtype]);
    }

  } else {
    // fprintf(stderr, "both matrices have entries\n");
    // Hardest case. Compare lists node by node. Let's make it simpler by requiring that both have the same default value
    if (!list_eqeq_list(left->rows, right->rows, left->default_val, right->default_val, left->dtype, left->rank-1, &num_checked)) {
    	return false;
    	
    } else if (num_checked < max_elements) {
    	return eqeq(left->default_val, right->default_val, 1, nm_sizeof[left->dtype]);
    }
  }

  return true;
}

/////////////
// Utility //
/////////////

/*
 * Documentation goes here.
 */
size_t list_storage_count_elements_r(const LIST* l, size_t recursions) {
  size_t count = 0;
  NODE* curr = l->first;
  
  if (recursions) {
    while (curr) {
      count += list_storage_count_elements_r(curr->val, recursions - 1);
      curr   = curr->next;
    }
    
  } else {
    while (curr) {
      ++count;
      curr = curr->next;
    }
  }
  
  return count;
}

/*
 * Count non-diagonal non-zero elements.
 */
size_t count_list_storage_nd_elements(const LIST_STORAGE* s) {
  NODE *i_curr, *j_curr;
  size_t count = 0;
  
  if (s->rank != 2) {
  	rb_raise(rb_eNotImpError, "non-diagonal element counting only defined for rank = 2");
  }

  for (i_curr = s->rows->first; i_curr; i_curr = i_curr->next) {
    for (j_curr = ((LIST*)(i_curr->val))->first; j_curr; j_curr = j_curr->next) {
      if (i_curr->key != j_curr->key) {
      	++count;
      }
    }
  }
  
  return count;
}

/////////////////////////
// Copying and Casting //
/////////////////////////

/*
 * Documentation goes here.
 */
LIST_STORAGE* list_storage_copy(LIST_STORAGE* rhs) {
  LIST_STORAGE* lhs;
  size_t* shape;
  void* default_val = ALLOC_N(char, nm_sizeof[rhs->dtype]);

  //fprintf(stderr, "copy_list_storage\n");

  // allocate and copy shape
  shape = ALLOC_N(size_t, rhs->rank);
  memcpy(shape, rhs->shape, rhs->rank * sizeof(size_t));
  memcpy(default_val, rhs->default_val, nm_sizeof[rhs->dtype]);

  lhs = create_list_storage(rhs->dtype, shape, rhs->rank, default_val);

  if (lhs) {
    lhs->rows = create_list();
    cast_copy_list_contents(lhs->rows, rhs->rows, rhs->dtype, rhs->dtype, rhs->rank - 1);
  } else {
  	free(shape);
  }

  return lhs;
}

/*
 * Documentation goes here.
 */
LIST_STORAGE* list_storage_cast_copy(LIST_STORAGE* rhs, int8_t new_dtype) {
  LIST_STORAGE* lhs;
  size_t* shape;
  void* default_val = ALLOC_N(char, nm_sizeof[rhs->dtype]);

  //fprintf(stderr, "copy_list_storage\n");

  // allocate and copy shape
  shape = ALLOC_N(size_t, rhs->rank);
  memcpy(shape, rhs->shape, rhs->rank * sizeof(size_t));

  // copy default value
  if (new_dtype == rhs->dtype) {
  	memcpy(default_val, rhs->default_val, nm_sizeof[rhs->dtype]);
  	
  } else {
  	SetFuncs[new_dtype][rhs->dtype](1, default_val, 0, rhs->default_val, 0);
  }

  lhs = create_list_storage(new_dtype, shape, rhs->rank, default_val);

  lhs->rows = create_list();
  cast_copy_list_contents(lhs->rows, rhs->rows, new_dtype, rhs->dtype, rhs->rank - 1);

  return lhs;
}

/*
 * Documentation goes here.
 */
LIST_STORAGE* list_storage_from_dense(const DENSE_STORAGE* rhs, int8_t l_dtype) {
  LIST_STORAGE* lhs;
  size_t pos = 0;
  void* l_default_val = ALLOC_N(char, nm_sizeof[l_dtype]);
  void* r_default_val = ALLOCA_N(char, nm_sizeof[rhs->dtype]); // clean up when finished with this function

  // allocate and copy shape and coords
  size_t *shape = ALLOC_N(size_t, rhs->rank), *coords = ALLOC_N(size_t, rhs->rank);
  memcpy(shape, rhs->shape, rhs->rank * sizeof(size_t));
  memset(coords, 0, rhs->rank * sizeof(size_t));

  // set list default_val to 0
  if (l_dtype == NM_ROBJ) {
  	*(VALUE*)l_default_val = INT2FIX(0);
  	
  } else {
  	memset(l_default_val, 0, nm_sizeof[l_dtype]);
  }

  // need test default value for comparing to elements in dense matrix
  if (rhs->dtype == l_dtype) {
  	r_default_val = l_default_val;
  	
  } else if (rhs->dtype == NM_ROBJ) {
  	*(VALUE*)r_default_val = INT2FIX(0);
  	
  } else {
  	memset(r_default_val, 0, nm_sizeof[rhs->dtype]);
  }

  lhs = create_list_storage(l_dtype, shape, rhs->rank, l_default_val);

  lhs->rows = create_list();
  cast_copy_list_contents_dense(lhs->rows, rhs->elements, r_default_val, l_dtype, rhs->dtype, &pos, coords, rhs->shape, rhs->rank, rhs->rank - 1);

  return lhs;
}

/*
 * Documentation goes here.
 */
LIST_STORAGE* list_storage_from_yale(const YALE_STORAGE* rhs, int8_t l_dtype) {
  LIST_STORAGE* lhs;
  NODE *last_added, *last_row_added = NULL;
  LIST* curr_row;
  y_size_t ija, ija_next, i, jj;
  bool add_diag;
  void* default_val = ALLOC_N(char, nm_sizeof[l_dtype]);
  void* R_ZERO = (char*)(rhs->a) + rhs->shape[0]*nm_sizeof[rhs->dtype];
  void* insert_val;

  // allocate and copy shape
  size_t *shape = ALLOC_N(size_t, rhs->rank);
  shape[0] = rhs->shape[0]; shape[1] = rhs->shape[1];

  // copy default value from the zero location in the Yale matrix
  SetFuncs[l_dtype][rhs->dtype](1, default_val, 0, R_ZERO, 0);

  lhs = create_list_storage(l_dtype, shape, rhs->rank, default_val);

  if (rhs->rank != 2) {
    rb_raise(nm_eStorageTypeError, "Can only convert matrices of rank 2 from yale.");
  }

  // Walk through rows and columns as if RHS were a dense matrix
  for (i = rhs->shape[0]; i-- > 0;) {

    // Get boundaries of beginning and end of row
    YaleGetIJA(ija, rhs, i);
    YaleGetIJA(ija_next, rhs, i+1);

    // Are we going to need to add a diagonal for this row?
    if (ElemEqEq[rhs->dtype][0]((char*)(rhs->a) + i*nm_sizeof[rhs->dtype], R_ZERO, 1, nm_sizeof[rhs->dtype])) {
    	// zero
    	add_diag = false;
    	
    } else {
    	// nonzero diagonal
    	add_diag = true;
    }
		
    if (ija < ija_next || add_diag) {

      curr_row = create_list();
      last_added = NULL;

      while (ija < ija_next) {
        YaleGetIJA(jj, rhs, ija); // what column number is this?

        // Is there a nonzero diagonal item between the previously added item and the current one?
        if (jj > i && add_diag) {
          // Allocate and copy insertion value
          insert_val = ALLOC_N(char, nm_sizeof[l_dtype]);
          SetFuncs[l_dtype][rhs->dtype](1, insert_val, 0, (char*)(rhs->a) + i*nm_sizeof[rhs->dtype], 0);
					
          // insert the item in the list at the appropriate location
          if (last_added) {
          	last_added = list_insert_after(last_added, i, insert_val);
          	
          } else {
          	last_added = list_insert(curr_row, false, i, insert_val);
          }
					
					// don't add again!
          add_diag = false;
        }

        // now allocate and add the current item
        insert_val = ALLOC_N(char, nm_sizeof[l_dtype]);
        SetFuncs[l_dtype][rhs->dtype](1, insert_val, 0, (char*)(rhs->a) + ija*nm_sizeof[rhs->dtype], 0);

        if (last_added) {
        	last_added = list_insert_after(last_added, jj, insert_val);
        	
        } else {
        	last_added = list_insert(curr_row, false, jj, insert_val);
        }

        ++ija; // move to next entry in Yale matrix
      }

      if (add_diag) {
      	// still haven't added the diagonal.
      	
        insert_val = ALLOC_N(char, nm_sizeof[l_dtype]);
        SetFuncs[l_dtype][rhs->dtype](1, insert_val, 0, (char*)(rhs->a) + i*nm_sizeof[rhs->dtype], 0);

        // insert the item in the list at the appropriate location
        if (last_added) {
        	last_added = list_insert_after(last_added, i, insert_val);
        	
        } else {
        	last_added = list_insert(curr_row, false, i, insert_val);
        }
      }

      // Now add the list at the appropriate location
      if (last_row_added) {
      	last_row_added = list_insert_after(last_row_added, i, curr_row);
      	
      } else {
      	last_row_added = list_insert(lhs->rows, false, i, curr_row);
      }
    }
	
		// end of walk through rows
  }

  return lhs;
}

/* Copy dense into lists recursively
 *
 * TODO: This works, but could probably be cleaner (do we really need to pass
 * 	coords around?)
 */
static bool list_storage_cast_copy_contents_dense(LIST* lhs, const char* rhs, void* zero, int8_t l_dtype, int8_t r_dtype, size_t* pos, size_t* coords, const size_t* shape, size_t rank, size_t recursions) {
  NODE *prev;
  LIST *sub_list;
  bool added = false, added_list = false;
  void* insert_value;

  for (coords[rank-1-recursions] = 0; coords[rank-1-recursions] < shape[rank-1-recursions]; ++coords[rank-1-recursions], ++(*pos)) {
    //fprintf(stderr, "(%u)\t<%u, %u>: ", recursions, coords[0], coords[1]);

    if (recursions == 0) {
    	// create nodes
    	
      if (!ElemEqEq[r_dtype][0]((char*)rhs + (*pos)*nm_sizeof[r_dtype], zero, 1, nm_sizeof[r_dtype])) {
      	// is not zero
        //fprintf(stderr, "inserting value\n");

        // Create a copy of our value that we will insert in the list
        insert_value = ALLOC_N(char, nm_sizeof[l_dtype]);
        cast_copy_value_single(insert_value, rhs + (*pos)*nm_sizeof[r_dtype], l_dtype, r_dtype);

        if (!lhs->first) {
        	prev = list_insert(lhs, false, coords[rank-1-recursions], insert_value);
        	
        } else {
        	 prev = list_insert_after(prev, coords[rank-1-recursions], insert_value);
        }
        
        added = true;
      } //else fprintf(stderr, "zero\n");
      // no need to do anything if the element is zero
      
    } else { // create lists
      //fprintf(stderr, "inserting list\n");
      // create a list as if there's something in the row in question, and then delete it if nothing turns out to be there
      sub_list = create_list();

      added_list = cast_copy_list_contents_dense(sub_list, rhs, zero, l_dtype, r_dtype, pos, coords, shape, rank, recursions-1);

      if (!added_list) {
      	delete_list(sub_list, recursions-1);
      	fprintf(stderr, "deleting list\n");
      	
      } else if (!lhs->first) {
      	prev = list_insert(lhs, false, coords[rank-1-recursions], sub_list);
      	
      } else {
      	prev = list_insert_after(prev, coords[rank-1-recursions], sub_list);
      }

      // added = (added || added_list);
    }
  }

  coords[rank-1-recursions] = 0;
  --(*pos);

  return added;
}
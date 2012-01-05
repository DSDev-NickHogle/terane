/*
 * Copyright 2010,2011 Michael Frank <msfrank@syntaxjockey.com>
 *
 * This file is part of Terane.
 *
 * Terane is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Terane is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Terane.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "backend.h"

/*
 * terane_Iter_dealloc: free resources for the Iter object.
 */
static void
_Iter_dealloc (terane_Iter *self)
{
    terane_Iter_close (self);
    self->ob_type->tp_free ((PyObject *) self);
}

/*
 * terane_Iter_new: allocate a new Iter object using the supplied DB cursor.
 * 
 * returns: A new Iter object
 */
PyObject *
terane_Iter_new (PyObject *parent, DBC *cursor, terane_Iter_ops *ops)
{
    terane_Iter *iter;

    assert (parent != NULL);
    assert (cursor != NULL);
    assert (ops != NULL);

    iter = PyObject_New (terane_Iter, &terane_IterType);
    if (iter == NULL)
        return NULL;
    Py_INCREF (parent);
    /* PyObject_New doesn't set fields to 0, so make sure we initialize all fields */
    iter->parent = parent;
    iter->cursor = cursor;
    iter->initialized = 0;
    iter->itype = TERANE_ITER_ALL;
    memset (&iter->start_key, 0, sizeof (DBT));
    memset (&iter->end_key, 0, sizeof (DBT));
    iter->next = ops->next;
    iter->skip = ops->skip;
    return (PyObject *) iter;
}

/*
 * terane_Iter_new_range: allocate a new Iter object using the supplied DB
 *  cursor.
 * 
 * returns: A new Iter object
 */
PyObject *
terane_Iter_new_range (PyObject *parent, DBC *cursor, terane_Iter_ops *ops, void *key, size_t len)
{
    terane_Iter *iter;

    iter = (terane_Iter *) terane_Iter_new (parent, cursor, ops);
    if (iter == NULL)
        return NULL;
    iter->itype = TERANE_ITER_RANGE;
    iter->start_key.data = PyMem_Malloc (len);
    /* allocation failed */
    if (iter->start_key.data == NULL) {
        Py_DECREF (iter);
        return PyErr_NoMemory ();
    }
    iter->start_key.size = len;
    memcpy (iter->start_key.data, key, len);
    return (PyObject *) iter;
}

/*
 * terane_Iter_new_from: allocate a new Iter object using the supplied DB cursor.
 * 
 * returns: A new Iter object
 */
PyObject *
terane_Iter_new_from (PyObject *parent, DBC *cursor, terane_Iter_ops *ops, void *key, size_t len)
{
    terane_Iter *iter;

    iter = (terane_Iter *) terane_Iter_new (parent, cursor, ops);
    if (iter == NULL)
        return NULL;
    iter->itype = TERANE_ITER_FROM;
    iter->start_key.data = PyMem_Malloc (len);
    if (iter->start_key.data == NULL) {
        Py_DECREF (iter);
        return PyErr_NoMemory ();
    }
    iter->start_key.size = len;
    memcpy (iter->start_key.data, key, len);
    return (PyObject *) iter;
}

/*
 * terane_Iter_new_within: allocate a new Iter object using the supplied DB cursor.
 * 
 * returns: A new Iter object
 */
PyObject *
terane_Iter_new_within (PyObject *parent, DBC *cursor, terane_Iter_ops *ops, DBT *start, DBT *end)
{
    terane_Iter *iter;

    iter = (terane_Iter *) terane_Iter_new (parent, cursor, ops);
    if (iter == NULL)
        return NULL;
    iter->itype = TERANE_ITER_WITHIN;

    iter->start_key.data = PyMem_Malloc (start->size);
    if (iter->start_key.data == NULL) {
        PyErr_NoMemory ();
        goto error;
    }
    iter->start_key.size = start->size;
    memcpy (iter->start_key.data, start->data, start->size);

    iter->end_key.data = PyMem_Malloc (end->size);
    if (iter->end_key.data == NULL) {
        PyErr_NoMemory ();
        goto error;
    }
    iter->end_key.size = end->size;
    memcpy (iter->end_key.data, end->data, end->size);

    return (PyObject *) iter;

error:
    if (iter->start_key.data)
        PyMem_Free (iter->start_key.data);
    if (iter->end_key.data)
        PyMem_Free (iter->end_key.data);
    Py_DECREF (iter);
    return NULL;
}

/*
 * _Iter_iter: return the iterator.
 */
static PyObject *
_Iter_iter (terane_Iter *self)
{
    Py_XINCREF (self);
    return (PyObject *) self;
}

/*
 * _Iter_get: retreive the iterator item from the cursor.
 */
static PyObject *
_Iter_get (terane_Iter *iter, DBT *key, int itype, int flags)
{
    DBT k, data;
    int cmp, dbret;
    PyObject *item = NULL;

    /* get the next cursor item */
    memcpy (&k, key, sizeof (DBT));
    k.flags |= DB_DBT_MALLOC;
    memset (&data, 0, sizeof (DBT));
    data.flags = DB_DBT_MALLOC;
    dbret = iter->cursor->get (iter->cursor, &k, &data, flags);    
    switch (dbret) {
        /* success */
        case 0:
            /* we have now completed one successful iteration */ 
            iter->initialized = 1;
            break;
        case DB_NOTFOUND:
            /* if no item is found, then return NULL to stop iterating */
            terane_Iter_close (iter);
            return NULL;
        /* for any other error, set exception and return NULL */
        case DB_LOCK_DEADLOCK:
            PyErr_Format (terane_Exc_Deadlock, "Failed to get next item: %s",
                db_strerror (dbret));
            return NULL;
        case DB_LOCK_NOTGRANTED:
            PyErr_Format (terane_Exc_LockTimeout, "Failed to get next item: %s",
                db_strerror (dbret));
            return NULL;
        default:
            PyErr_Format (terane_Exc_Error, "Failed to get next item: %s",
                db_strerror (dbret));
            return NULL;
    }
    /* perform post-retrieval checks */
    switch (itype) {
        case TERANE_ITER_RANGE:
            /* check that the key prefix matches */
            if (iter->start_key.size <= k.size &&
              memcmp (iter->start_key.data, k.data, iter->start_key.size) == 0)
                item = iter->next (iter, &k, &data);
            break;
        case TERANE_ITER_WITHIN:
            /* check that the key is between the start and end keys */
            cmp = memcmp (k.data, iter->end_key.data, 
                iter->end_key.size < k.size ? iter->end_key.size : k.size);
            if (cmp < 0 || (cmp == 0 && k.size > iter->end_key.size))
                item = iter->next (iter, &k, &data);
            break;
        default:
            item = iter->next (iter, &k, &data);
            break;
    }
    /* free allocated memory */
    if (k.data && k.data != key->data)
        PyMem_Free (k.data);
    if (data.data)
        PyMem_Free (data.data);
    /* if the callback returned NULL, then close the cursor */
    if (item == NULL)
        terane_Iter_close (iter);

    return item;
}

/*
 * _Iter_next: return the next iterator item.
 */
static PyObject *
_Iter_next (terane_Iter *self)
{
    DBT key;
    uint32_t flags = 0;

    if (self->cursor == NULL)
        return PyErr_Format (terane_Exc_Error, "iterator is closed");
    if (self->next == NULL)
        return PyErr_Format (terane_Exc_Error, "No next callback for iterator");

    /* initialize the cursor position, if necessary */
    memset (&key, 0, sizeof (DBT));
    switch (self->itype)
    {
        case TERANE_ITER_ALL:
            if (!self->initialized)
                flags = DB_FIRST;
            else
                flags = DB_NEXT;
            break;
        case TERANE_ITER_RANGE:
        case TERANE_ITER_FROM:
        case TERANE_ITER_WITHIN:
            if (!self->initialized) {
                /*
                 * if this is a range search, or if we are starting from
                 * a specified key, then set the key before searching
                 */
                key.data = self->start_key.data;
                key.size = self->start_key.size;
                flags = DB_SET_RANGE;
            }
            else
                flags = DB_NEXT;
            break;
    }
    return _Iter_get (self, &key, self->itype, flags);
}

/*
 * terane_Iter_skip: Move the iterator to the specified item.
 *
 * callspec: Iter.skip(item)
 * parameters:
 *   item (object): A python object that describes the item to skip to
 * returns: The iterator value at the skipped-to position
 * exceptions:
 *  IndexError: Target item is out of range
 *  terane.outputs.store.backend.Error: failed to move the DBC cursor
 */
PyObject *
terane_Iter_skip (terane_Iter *self, PyObject *args)
{
    DBT *key = NULL;
    PyObject *item = NULL;

    if (self->cursor == NULL)
        return PyErr_Format (terane_Exc_Error, "iterator is closed");
    if (self->skip == NULL)
        return PyErr_Format (terane_Exc_Error, "No skip callback for iterator");

    /* generate the key to skip to */
    key = self->skip (self, args);
    if (key == NULL)
        return NULL;
    /* retrieve the item associated with the key */
    item = _Iter_get (self, key, TERANE_ITER_RANGE, DB_SET_RANGE);
    /* free key data */
    if (key->data != NULL)
        PyMem_Free (key->data);
    PyMem_Free (key);
    if (item == NULL)
        return PyErr_Format (PyExc_IndexError, "Target is out of range");
    return item;
}
/*
 * terane_Iter_reset: Move the iterator back to the beginning.
 *
 * callspec: Iter.reset()
 * parameters: None
 * returns: None
 * exceptions:
 *  terane.outputs.store.backend.Error: Iterator is closed
 */
PyObject *
terane_Iter_reset (terane_Iter *self)
{
    if (self->cursor == NULL)
        return PyErr_Format (terane_Exc_Error, "iterator is closed");
    self->initialized = 0;
    Py_RETURN_NONE;
}

/*
 * terane_Iter_close: close the underlying DB Cursor.
 *
 * callspec: Iter.close()
 * parameters: None
 * returns: None
 * exceptions:
 *  terane.outputs.store.backend.Deadlock: the DBC cursor was chosen by deadlock detector
 *  terane.outputs.store.backend.LockTimeout:
 *  terane.outputs.store.backend.Error: failed to close the DBC cursor
 */
PyObject *
terane_Iter_close (terane_Iter *self)
{
    if (self->cursor != NULL) {
        int dbret = self->cursor->close (self->cursor);
        switch (dbret) {
            case 0:
                break;
            case DB_LOCK_DEADLOCK:
                PyErr_Format (terane_Exc_Deadlock, "Failed to close Iter: %s", db_strerror (dbret));
                break;
            case DB_LOCK_NOTGRANTED:
                PyErr_Format (terane_Exc_LockTimeout, "Failed to close Iter: %s", db_strerror (dbret));
                break;
            default:
                PyErr_Format (terane_Exc_Error, "Failed to close Iter: %s", db_strerror (dbret));
                break;
        }
        self->cursor = NULL;
    }
    if (self->parent)
        Py_DECREF (self->parent);
    self->parent = NULL;
    if (self->start_key.data)
        PyMem_Free (self->start_key.data);
    if (self->end_key.data)
        PyMem_Free (self->end_key.data);
    memset (&self->start_key, 0, sizeof (DBT));
    memset (&self->end_key, 0, sizeof (DBT));
    Py_RETURN_NONE;
}

/* Iter methods declaration */
PyMethodDef _Iter_methods[] =
{
    { "skip", (PyCFunction) terane_Iter_skip, METH_VARARGS,
        "Move the iterator to the specified item." },
    { "reset", (PyCFunction) terane_Iter_reset, METH_NOARGS,
        "Move the iterator back to the beginning." },
    { "close", (PyCFunction) terane_Iter_close, METH_NOARGS,
        "Free resources allocated by the iterator." },
    { NULL, NULL, 0, NULL }
};

/* Iter type declaration */
PyTypeObject terane_IterType = {
    PyObject_HEAD_INIT(NULL)
    0,
    "backend.Iter",
    sizeof (terane_Iter),
    0,                         /*tp_itemsize*/
    (destructor) _Iter_dealloc,
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "Generic DB Iterator",     /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    (getiterfunc) _Iter_iter,
    (iternextfunc) _Iter_next,
    _Iter_methods,
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
};

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
 * _Segment_dealloc: free resources for the Segment object.
 */
static void
_Segment_dealloc (terane_Segment *self)
{
    _Segment_close (self);
    if (self->env != NULL)
        Py_DECREF (self->env);
    self->env = NULL;
    if (self->toc != NULL)
        Py_DECREF (self->toc);
    self->toc = NULL;
    if (self->name != NULL)
        PyMem_Free (self->name);
    self->name = NULL;
    self->ob_type->tp_free ((PyObject *) self);
}

/*
 * _Segment_new: allocate a new Segment object.
 *
 * callspec: Segment(toc, sid)
 * parameters:
 *  toc (TOC): A TOC object to use for bookkeeping
 *  sid (long): The segment id
 * returns: A new Segment object
 * exceptions:
 *  terane.outputs.store.backend.Error: failed to create/open the Segment
 */
static PyObject *
_Segment_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    terane_Segment *self;
    DB_TXN *txn = NULL;
    db_recno_t segment_id = 0;
    int exists, dbret;

    /* allocate the Segment object */
    self = (terane_Segment *) type->tp_alloc (type, 0);
    if (self == NULL) {
        PyErr_SetString (terane_Exc_Error, "Failed to allocate Segment");
        return NULL;
    }
    self->toc = NULL;
    self->env = NULL;
    self->name = NULL;
    self->metadata = NULL;
    self->documents = NULL;
    self->fields = NULL;
    self->nfields = 0;
    self->deleted = 0;

    /* parse constructor parameters */
    if (!PyArg_ParseTuple (args, "O!k", &terane_TOCType, &self->toc, &segment_id))
        goto error;
    Py_INCREF (self->toc);

    /* verify the segment exists in the TOC */
    exists = terane_TOC_contains_segment (self->toc, NULL, segment_id);
    if (exists < 0)
        goto error;
    if (exists == 0) {
        PyErr_Format (PyExc_KeyError, "Segment %lu doesn't exist",
            (unsigned long int) segment_id);
        goto error;
    }

    /* allocate a buffer large enough to hold the longest segment name */
    self->name = PyMem_Malloc (PyString_Size (self->toc->name) + 12);
    if (self->name == NULL) {
        PyErr_NoMemory ();
        goto error;
    }
    sprintf (self->name, "%s.%lu", PyString_AsString (self->toc->name),
        (unsigned long int) segment_id);

    /* wrap db creation in a transaction */
    self->env = self->toc->env;
    Py_INCREF (self->env);
    dbret = self->env->env->txn_begin (self->env->env, NULL, &txn, 0);
    if (dbret != 0) {
        PyErr_Format (terane_Exc_Error, "Failed to create transaction: %s",
            db_strerror (dbret));
        goto error;
    }

    /* create the DB handle for metadata */
    dbret = db_create (&self->metadata, self->env->env, 0);
    if (dbret != 0) {
        PyErr_Format (terane_Exc_Error, "Failed to create handle for _metadata: %s",
            db_strerror (dbret));
        goto error;
    }

    /* open the metadata DB */
    dbret = self->metadata->open (self->metadata, txn, self->name,
        "_metadata", DB_BTREE, DB_CREATE | DB_THREAD, 0);
    if (dbret != 0) {
        PyErr_Format (terane_Exc_Error, "Failed to open _metadata: %s",
            db_strerror (dbret));
        goto error;
    }

    /* create the DB handle for documents */
    dbret = db_create (&self->documents, self->env->env, 0);
    if (dbret != 0) {
        PyErr_Format (terane_Exc_Error, "Failed to create handle for _documents: %s",
            db_strerror (dbret));
        goto error;
    }

    /* open the documents DB */
    dbret = self->documents->open (self->documents, txn, self->name,
        "_documents", DB_BTREE, DB_CREATE | DB_THREAD, 0);
    if (dbret != 0) {
        PyErr_Format (terane_Exc_Error, "Failed to open _documents: %s",
            db_strerror (dbret));
        goto error;
    }

    /* commit new databases */
    dbret = txn->commit (txn, 0);
    if (dbret != 0) {
        PyErr_Format (terane_Exc_Error, "Failed to commit transaction: %s",
            db_strerror (dbret));
        txn = NULL;
        goto error;
    }

    /* return the initialized Segment object on success */
    return (PyObject *) self;

/* if there is an error, then free any locally allocated memory and references */
error:
    if (txn != NULL)
        txn->abort (txn);
    if (self)
        _Segment_dealloc ((terane_Segment *) self);
    return NULL;
}

/*
 * terane_Segment_delete: Mark the Segment for deletion.
 */
PyObject *
terane_Segment_delete (terane_Segment *self)
{
    self->deleted = 1;
    Py_RETURN_NONE;
}

/*
 * terane_Segment_close: close the underlying DB handles.
 *
 * callspec: Segment.close()
 * parameters: None
 * returns: None
 * exceptions:
 *  terane.outputs.store.backend.Error: failed to close a db in the Segment
 */
PyObject *
terane_Segment_close (terane_Segment *self)
{
    int i, dbret;
    terane_Field *field;

    /* close the metadata db */
    if (self->metadata != NULL) {
        dbret = self->metadata->close (self->metadata, 0);
        if (dbret != 0)
            PyErr_Format (terane_Exc_Error, "Failed to close _metadata DB: %s",
                db_strerror (dbret));
    }
    self->metadata = NULL;

    /* close the documents db */
    if (self->documents != NULL) {
        dbret = self->documents->close (self->documents, 0);
        if (dbret != 0)
            PyErr_Format (terane_Exc_Error, "Failed to close _documents DB: %s",
                db_strerror (dbret));
    }
    self->documents = NULL;

    /* close each field db */
    if (self->fields != NULL) {
        for (i = 0; i < self->nfields; i++) {
            field = self->fields[i];
            if (field != NULL) {
                if (field->field != NULL) {
                    dbret = field->field->close (field->field, 0);
                    if (dbret != 0)
                        PyErr_Format (terane_Exc_Error, "Failed to close segment field '%s': %s",
                            PyString_AsString (field->name), db_strerror (dbret));
                }
                field->field = NULL;
                if (field->name != NULL)
                    Py_DECREF (field->name);
                field->name = NULL;
                PyMem_Free (field);
            }
        }
        PyMem_Free (self->fields);
    }
    self->fields = NULL;
    self->nfields = 0;

    /* if this segment is marked to be deleted */
    if (self->deleted) {
        dbret = self->env->env->dbremove (self->env->env, NULL,
            self->name, NULL, DB_AUTO_COMMIT);
        if (dbret != 0)
            PyErr_Format (terane_Exc_Error, "Failed to delete segment: %s",
                db_strerror (dbret));
    }

    Py_RETURN_NONE;
}

/* Segment methods declaration */
PyMethodDef _Segment_methods[] =
{
    { "get_meta", (PyCFunction) terane_Segment_get_meta, METH_VARARGS,
        "Get a Segment metadata value." },
    { "set_meta", (PyCFunction) terane_Segment_set_meta, METH_VARARGS,
        "Set a Segment metadata value." },
    { "get_field_meta", (PyCFunction) terane_Segment_get_field_meta, METH_VARARGS,
        "Get a field metadata value." },
    { "set_field_meta", (PyCFunction) terane_Segment_set_field_meta, METH_VARARGS,
        "Set a field metadata value." },
    { "new_doc", (PyCFunction) terane_Segment_new_doc, METH_VARARGS,
        "Create a new document." },
    { "get_doc", (PyCFunction) terane_Segment_get_doc, METH_VARARGS,
        "Get a document blob by document ID." },
    { "set_doc", (PyCFunction) terane_Segment_set_doc, METH_VARARGS,
        "Set a document blob value." },
    { "delete_doc", (PyCFunction) terane_Segment_delete_doc, METH_VARARGS,
        "Delete a document blob." },
    { "contains_doc", (PyCFunction) terane_Segment_contains_doc, METH_VARARGS,
        "Returns True if the segment contains the specified document." },
    { "iter_docs", (PyCFunction) terane_Segment_iter_docs, METH_VARARGS,
        "Iterates through all documents in the index." },
    { "first_doc", (PyCFunction) terane_Segment_first_doc, METH_VARARGS,
        "Return the first (lowest numbered) document." },
    { "last_doc", (PyCFunction) terane_Segment_last_doc, METH_VARARGS,
        "Return the last (highest numbered) document." },
    { "get_word", (PyCFunction) terane_Segment_get_word, METH_VARARGS,
        "Get a word in the inverted index." },
    { "set_word", (PyCFunction) terane_Segment_set_word, METH_VARARGS,
        "Set a word in the inverted index." },
    { "contains_word", (PyCFunction) terane_Segment_contains_word, METH_VARARGS,
        "Returns True if the segment contains the specified word." },
    { "iter_words", (PyCFunction) terane_Segment_iter_words, METH_VARARGS,
        "Iterates through all words in the index." },
    { "get_word_meta", (PyCFunction) terane_Segment_get_word_meta, METH_VARARGS,
        "Get metadata for a word in the inverted index." },
    { "set_word_meta", (PyCFunction) terane_Segment_set_word_meta, METH_VARARGS,
        "Set metadata for a word in the inverted index." },
    { "iter_words_meta", (PyCFunction) terane_Segment_iter_words_meta, METH_VARARGS,
        "Iterates through all words in the index." },
    { "iter_words_meta_from", (PyCFunction) terane_Segment_iter_words_meta_from, METH_VARARGS,
        "Iterates through words in the index, starting from the specified word." },
    { "iter_words_meta_range", (PyCFunction) terane_Segment_iter_words_meta_range, METH_VARARGS,
        "Iterates through all words in the index matching the prefix." },
    { "delete", (PyCFunction) terane_Segment_delete, METH_NOARGS,
        "Mark the DB Segment for deletion.  Actual deletion will not occur until the Segment is deallocated." },
    { "close", (PyCFunction) terane_Segment_close, METH_NOARGS,
        "Close the DB Segment." },
    { NULL, NULL, 0, NULL }
};

/* Segment type declaration */
PyTypeObject terane_SegmentType = {
    PyObject_HEAD_INIT(NULL)
    0,
    "backend.Segment",
    sizeof (terane_Segment),
    0,                         /*tp_itemsize*/
    (destructor) _Segment_dealloc,
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
    "DB Segment",                /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    _Segment_methods,
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    _Segment_new               /* tp_new */
};

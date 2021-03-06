#include "backend.h"

/* backend module function table */
static PyMethodDef backend_functions[] =
{
    { "log_fd", (PyCFunction) terane_log_fd, METH_NOARGS,
        "Return the reading end of the logger channel." },
    { "msgpack_dump", (PyCFunction) terane_msgpack_dump, METH_VARARGS,
        "Serialize the object." },
    { "msgpack_load", (PyCFunction) terane_msgpack_load, METH_VARARGS,
        "Deserialize the object." },
    { NULL, NULL, 0, NULL }
};

PyObject *terane_Exc_Deadlock = NULL;
PyObject *terane_Exc_LockTimeout = NULL;
PyObject *terane_Exc_DocExists = NULL;
PyObject *terane_Exc_Error = NULL;

/* backend module init function */
PyMODINIT_FUNC
initbackend (void)
{
    PyObject *m;
    int dbret;

    /* set berkeley db to use the python memory allocation functions */
    if ((dbret = db_env_set_func_malloc (PyMem_Malloc)) != 0) {
        PyErr_Format (PyExc_SystemError, "Failed to set internal memory routines: %s", db_strerror (dbret));
        return;
    }
    if ((dbret = db_env_set_func_realloc (PyMem_Realloc)) != 0) {
        PyErr_Format (PyExc_SystemError, "Failed to set internal memory routines: %s", db_strerror (dbret));
        return;
    }
    if ((dbret = db_env_set_func_free (PyMem_Free)) != 0) {
        PyErr_Format (PyExc_SystemError, "Failed to set internal memory routines: %s", db_strerror (dbret));
        return;
    }

    /* verify the object types are ready to load */
    if (PyType_Ready (&terane_EnvType) < 0)
        return;
    if (PyType_Ready (&terane_IndexType) < 0)
        return;
    if (PyType_Ready (&terane_SegmentType) < 0)
        return;
    if (PyType_Ready (&terane_TxnType) < 0)
        return;
    if (PyType_Ready (&terane_IterType) < 0)
        return;

    /* initialize the backend module */
    m = Py_InitModule3 ("backend", backend_functions, "Manipulate the terane database");

    /* load the types into the module */
    Py_INCREF (&terane_EnvType);
    PyModule_AddObject (m, "Env", (PyObject *) &terane_EnvType);
    Py_INCREF (&terane_IndexType);
    PyModule_AddObject (m, "Index", (PyObject *) &terane_IndexType);
    Py_INCREF (&terane_SegmentType);
    PyModule_AddObject (m, "Segment", (PyObject *) &terane_SegmentType);
    Py_INCREF (&terane_TxnType);
    PyModule_AddObject (m, "Txn", (PyObject *) &terane_TxnType);
    Py_INCREF (&terane_IterType);
    PyModule_AddObject (m, "Iter", (PyObject *) &terane_IterType);

    /* create exceptions */
    terane_Exc_Deadlock = PyErr_NewException("backend.Deadlock", NULL, NULL);
    Py_INCREF (terane_Exc_Deadlock);
    PyModule_AddObject (m, "Deadlock", terane_Exc_Deadlock);

    terane_Exc_LockTimeout = PyErr_NewException("backend.LockTimeout", NULL, NULL);
    Py_INCREF (terane_Exc_LockTimeout);
    PyModule_AddObject (m, "LockTimeout", terane_Exc_LockTimeout);

    terane_Exc_DocExists = PyErr_NewException("backend.DocExists", NULL, NULL);
    Py_INCREF (terane_Exc_DocExists);
    PyModule_AddObject (m, "DocExists", terane_Exc_DocExists);

    terane_Exc_Error = PyErr_NewException("backend.Error", NULL, NULL);
    Py_INCREF (terane_Exc_Error);
    PyModule_AddObject (m, "Error", terane_Exc_Error);
}

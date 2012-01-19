# Copyright 2010,2011 Michael Frank <msfrank@syntaxjockey.com>
#
# This file is part of Terane.
#
# Terane is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# Terane is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with Terane.  If not, see <http://www.gnu.org/licenses/>.

import pickle, time
from zope.interface import implements
from terane.bier import IWriter
from terane.bier.writing import WriterError
from terane.outputs.store.encoding import json_encode, json_decode
from terane.loggers import getLogger

logger = getLogger('terane.outputs.store.writing')

class WriterExpired(WriterError):
    pass

class IndexWriter(object):

    implements(IWriter)

    def __init__(self, ix):
        self._txn = None
        self._ix = ix
        self._indexSize = self._ix._indexSize
        self._currentSize = self._ix._currentSize
        self._lastId = self._ix._lastId
        self._lastModified = self._ix._lastModified

    def __enter__(self):
        if self._txn:
            raise WriterError("IndexWriter is already in a transaction")
        self._txn = self._ix.new_txn()
        return self

    def newDocument(self, docId, document):
        segment = self._ix._current
        segment.set_doc(self._txn, str(docId), json_encode(document))
        self._indexSize += 1
        self._currentSize += 1
        self._lastId = str(docId)
        self._lastModified = int(time.time())
        lastUpdate = {'size': self._currentSize, 'last-id': self._lastId, 'last-modified': self._lastModified}
        segment.set_meta(self._txn, 'last-update', json_encode(lastUpdate))

    def newPosting(self, fieldname, term, docId, value):
        segment = self._ix._current
        try:
            tmeta = json_decode(segment.get_term_meta(self._txn, fieldname, term))
            if not 'num-docs' in tmeta:
                raise WriterError("term metadata corruption: no such key 'num-docs'")
            tmeta['num-docs'] += 1
        except KeyError:
            tmeta = {'num-docs': 1}
        # increment the document count for this term
        segment.set_term_meta(self._txn, fieldname, term, json_encode(tmeta))
        try:
            fmeta = json_decode(segment.get_field_meta(self._txn, fieldname))
            if not 'num-docs' in fmeta:
                raise WriterError("field metadata corruption: no such key 'num-docs'")
            fmeta['num-docs'] += 1
        except KeyError:
            fmeta = {'num-docs': 1}
        # increment the document count for this field
        segment.set_field_meta(self._txn, fieldname, json_encode(fmeta))
        if value == None:
            value = dict()
        # add the term to the reverse index
        segment.set_term(self._txn, fieldname, term, str(docId), json_encode(value))

    def __exit__(self, excType, excValue, traceback):
        if (excType, excValue, traceback) == (None, None, None):
            self._txn.commit()
            self._ix._indexSize = self._indexSize
            self._ix._currentSize = self._currentSize
            self._ix._lastId = self._lastId
            self._ix._lastModified = self._lastModified
        else:
            self._txn.abort()
        return False

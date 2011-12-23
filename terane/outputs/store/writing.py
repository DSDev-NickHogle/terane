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
#
# ----------------------------------------------------------------------
#
# This file contains portions of code Copyright 2009 Matt Chaput
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#    http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import pickle, time
from zope.interface import implements
from terane.bier.writing import IWriter, WriterError
from terane.outputs.store.encoding import json_encode, json_decode
from terane.loggers import getLogger

logger = getLogger('terane.outputs.store.writing')

class WriterExpired(WriterError):
    pass

class IndexWriter(object):

    implements(IWriter)

    def __init__(self, ix):
        self._ix = ix
        self._segment = ix._current[0]
        self._txn = None

    def __enter__(self):
        if self._txn:
            raise WriterError("IndexWriter is already in a transaction")
        self._txn = self._ix.new_txn()
        return self

    def newDocument(self, docId, document):
        self._segment.set_doc(self._txn, docId, json_encode(document))
        try:
            last_update = json_decode(self._segment.get_meta(self._txn, 'last-update'))
            if 'size' not in last_update:
                raise WriterError("segment metadata corruption: no such key 'size'")
            last_update['size'] += 1
        except KeyError:
            last_update = {'size': 1}
        last_update['last-id'] = docId
        last_update['last-modified'] = int(time.time())
        self._segment.set_meta(self._txn, 'last-update', json_encode(last_update))

    def newPosting(self, fieldname, term, docId, value):
        try:
            tmeta = json_decode(self._segment.get_word_meta(self._txn, fieldname, term))
            if not 'num-docs' in tmeta:
                raise WriterError("term metadata corruption: no such key 'num-docs'")
            tmeta['num-docs'] += 1
        except KeyError:
            tmeta = {'num-docs': 1}
        # increment the document count for this field
        logger.trace("field=%s,doc=%s,term=%s: value=%s" % (fieldname,docId,term,value))
        self._segment.set_word_meta(self._txn, fieldname, term, json_encode(tmeta))
        if value == None:
            value = ''
        else:
            value = json_encode(value)
        self._segment.set_word(self._txn, fieldname, term, docId, value)

    def __exit__(self, excType, excValue, traceback):
        if (excType, excValue, traceback) == (None, None, None):
            self._txn.commit()
        else:
            self._txn.abort()
        self._ix = None
        self._segment = None
        self._txn = None
        return False

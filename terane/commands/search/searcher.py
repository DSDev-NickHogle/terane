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

import os, sys, datetime, dateutil.tz, xmlrpclib
from getpass import getpass
from logging import StreamHandler, DEBUG, Formatter
from pprint import pformat
from twisted.web.xmlrpc import Proxy
from twisted.web.error import Error as TwistedWebError
from twisted.internet import reactor
from terane.bier.evid import EVID
from terane.loggers import getLogger, startLogging, StdoutHandler, DEBUG

logger = getLogger('terane.commands.search.searcher')

class Searcher(object):
    def configure(self, settings):
        # load configuration
        section = settings.section("search")
        self.host = section.getString("host", 'localhost:45565')
        self.username = section.getString("username", None)
        self.password = section.getString("password", None)
        if section.getBoolean("prompt password", False):
            self.password = getpass("Password: ")
        self.limit = section.getInt("limit", 100)
        self.reverse = section.getBoolean("display reverse", False)
        self.longfmt = section.getBoolean("long format", False)
        self.indices = section.getList(str, "use indices", None)
        self.tz = section.getString("timezone", None)
        if self.tz != None:
            self.tz = dateutil.tz.gettz(self.tz)
        # get the list of fields to display
        self.fields = section.getList(str, "display fields", None)
        # concatenate the command args into the query string
        self.query = ' '.join(settings.args())
        # configure server logging
        logconfigfile = section.getString('log config file', "%s.logconfig" % settings.appname)
        if section.getBoolean("debug", False):
            startLogging(StdoutHandler(), DEBUG, logconfigfile)
        else:
            startLogging(None)

    def run(self):
        proxy = Proxy("http://%s/XMLRPC" % self.host, user=self.username,
            password=self.password, allowNone=True)
        deferred = proxy.callRemote('iterEvents', self.query, None, self.indices,
            self.limit, self.reverse, self.fields)
        deferred.addCallback(self.printResult)
        deferred.addErrback(self.printError)
        reactor.run()
        return 0

    def printResult(self, result):
        logger.debug("XMLRPC result: %s" % pformat(result))
        if len(result) > 0:
            meta = result['meta']
            data = result['data']
            for evid,defaultfield,defaultvalue,fields in data:
                ts = datetime.datetime.fromtimestamp(evid[0], dateutil.tz.tzutc())
                if self.tz:
                    ts = ts.astimezone(self.tz)
                print "%s: %s" % (ts.strftime("%d %b %Y %H:%M:%S %Z"), defaultvalue)
                if self.longfmt:
                    for fieldname,value in sorted(fields.items(), key=lambda x: x[0]):
                        if self.fields and fieldname not in self.fields:
                            continue
                        print "\t%s=%s" % (fieldname,value)
            print ""
            print "found %i matches in %f seconds." % (len(data), meta['runtime'])
        else:
            print "no matches found."
        reactor.stop()
 
    def printError(self, failure):
        try:
            raise failure.value
        except xmlrpclib.Fault, e:
            print "Search failed: %s (code %i)" % (e.faultString,e.faultCode)
        except ValueError, e:
            print "Search failed: remote server returned HTTP status %s: %s" % e.args
        except BaseException, e:
            print "Search failed: %s" % str(e)
        reactor.stop()

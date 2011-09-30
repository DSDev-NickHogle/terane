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

import os, traceback
from twisted.application.service import Service, MultiService
from pkg_resources import Environment, working_set
from terane.loggers import getLogger

logger = getLogger('terane.plugins')

class Plugin(Service):
    """
    Abstract class which all plugins inherit from.
    """

    def configure(self, section):
        """
        Subclasses should implement this method to parse configuration from
        the configuration file and command-line arguments.  If unimplemented,
        the default implementation does nothing.

        :param section: The configuration section.
        :type section: :class:`terane.settings.Section`
        """
        pass

    def startService(self):
        """
        Called when the plugin is started.  If a plugin needs to perform any
        startup tasks, they should override this method (be sure to chain up
        to the parent method) and perform them here.
        """
        Service.startService(self)

    def stopService(self):
        """
        Called when the plugin is stopped.  If a plugin needs to perform any
        shutdown task, they should override this method (be sure to chain up
        to the parent method) and perform them here.
        """
        Service.stopService(self)

class PluginManager(MultiService):
    """
    Manages the lifecycle of all plugins.  Each plugin is loaded during the
    configure phase, and its configure method is called.  When terane enters
    the main loop, the startService method of each plugin is called.  When
    terane exits the main loop, the stopService method of each plugin is called.
    """

    def __init__(self):
        MultiService.__init__(self)
        self.setName("plugins")
        self._plugins = {}
        
    def configure(self, settings):
        """
        Finds all discoverable plugins and configures them.  Plugins are
        discoverable if they are in the normal python module path, or in
        the path specified by 'plugin directory'.
        """
        # load plugins
        section = settings.section('server')
        self.pluginsdir = os.path.join(section.getPath("plugin directory"))
        if self.pluginsdir:
            logger.debug("loading plugins from %s" % self.pluginsdir)
            working_set.add_entry(self.pluginsdir)
            env = Environment([self.pluginsdir,])
        else:
            env = Environment([])
        self._eggs,errors = working_set.find_plugins(env)
        # load plugin eggs
        for p in self._eggs:
            working_set.add(p)
            logger.info("loaded plugin egg '%s'" % p)
        for e in errors:
            logger.info("failed to load plugin egg '%s'" % e)
        # load all discovered plugins for each type
        for ptype in ['protocol','input','output','filter']:
            plugins = {}
            for ep in working_set.iter_entry_points("terane.plugin.%s" % ptype):
                try:
                    # if a configuration section exists, load and configure the plugin
                    if settings.hasSection("plugin:%s:%s" % (ptype, ep.name)):
                        _Plugin = ep.load()
                        section = settings.section("plugin:%s:%s" % (ptype, ep.name))
                        plugin = _Plugin()
                        plugin.setName("plugin:%s:%s" % (ptype, ep.name))
                        plugin.configure(section)
                        self.addService(plugin)
                        plugins[ep.name] = plugin
                        logger.info("loaded %s plugin '%s'" % (ptype, ep.name))
                except Exception, e:
                    tb = "\nUnhandled Exception:\n%s\n---\n%s" % (e,traceback.format_exc())
                    logger.warning("failed to load %s plugin '%s':%s" % (ptype, ep.name, tb))
            self._plugins[ptype] = plugins

    def instance(self, ptype, pname):
        """
        Returns an instance of the specified plugin.
        
        :param ptype: The type of plugin.
        :type input_type: str
        :param pname: The name of the plugin.
        :type input_type: str
        :returns: An instance of the specified plugin.
        :rtype: :class:`terane.plugins.Plugin`
        :raises Exception: The specified plugin was not found.
        """
        try:
            plugins = self._plugins[ptype]
        except:
            raise Exception("no registered plugin type named '%s'" % ptype)
        try:
            plugin = plugins[pname]
        except:
            raise Exception("no registered %s plugin named '%s'" % (ptype, pname))
        return plugin.instance()


plugins = PluginManager()
"""
`plugins` is a singleton instance of a :class:`PluginManager`.  All interaction
with the plugin infrastructure must occur through this instance; do *not* instantiate
new :class:`PluginManager` instances!
"""

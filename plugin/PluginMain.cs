﻿using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

using Sphere.Plugins;
using Sphere.Plugins.Interfaces;
using minisphere.Gdk.DockPanes;
using minisphere.Gdk.Plugins;
using minisphere.Gdk.SettingsPages;

namespace minisphere.Gdk
{
    public class PluginMain : IPluginMain
    {
        public string Name { get { return "minisphere GDK"; } }
        public string Author { get { return "Fat Cerberus"; } }
        public string Description { get { return "Provides support for the minisphere GDK toolchain."; } }
        public string Version { get { return "2.0.1"; } }

        internal PluginConf Conf { get; private set; }
        internal int Sessions { get; set; }

        public void Initialize(ISettings conf)
        {
            Conf = new PluginConf(conf);
            Sessions = 0;

            PluginManager.Register(this, new CellCompiler(this), "Cell");
            PluginManager.Register(this, new minisphereStarter(this), "minisphere");
            PluginManager.Register(this, new SettingsPage(this), "minisphere GDK");

            Panes.Initialize(this);

            PluginManager.Core.UnloadProject += on_UnloadProject;
        }

        public void ShutDown()
        {
            PluginManager.Core.UnloadProject -= on_UnloadProject;
            PluginManager.UnregisterAll(this);
        }

        private void on_UnloadProject(object sender, EventArgs e)
        {
            Panes.Errors.Clear();
            Panes.Console.Clear();
        }
    }

    class PluginConf
    {
        public PluginConf(ISettings conf)
        {
            Conf = conf;
        }

        public ISettings Conf { get; private set; }

        public string GdkPath
        {
            get
            {
                RegistryKey key = Registry.LocalMachine.OpenSubKey(
                    @"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{10C19C9F-1E29-45D8-A534-8FEF98C7C2FF}_is1");
                if (key != null)
                {
                    // minisphere GDK is installed, get path from registry
                    string defaultPath = (string)key.GetValue(@"InstallLocation") ?? "";
                    string path = Conf.GetString("gdkPath", defaultPath);
                    return !string.IsNullOrWhiteSpace(path) ? path : defaultPath;
                }
                else
                {
                    // no installation key, just read from conf
                    return Conf.GetString("gdkPath", "");
                }
            }
            set
            {
                Conf.SetValue("gdkPath", value);
            }
        }

        public bool AlwaysUseConsole
        {
            get { return Conf.GetBoolean("alwaysUseConsole", false); }
            set { Conf.SetValue("alwaysUseConsole", value); }
        }

        public bool MakeDebugPackages
        {
            get { return Conf.GetBoolean("makeDebugPackages", false); }
            set { Conf.SetValue("makeDebugPackages", value); }
        }
    }

    static class Panes
    {
        public static void Initialize(PluginMain main)
        {
            PluginManager.Register(main, Inspector = new InspectorPane(), "Inspector");
            PluginManager.Register(main, Console = new ConsolePane(main.Conf), "Debug Console");
            PluginManager.Register(main, Errors = new ErrorPane(), "Exceptions");
        }

        public static ConsolePane Console { get; private set; }
        public static ErrorPane Errors { get; private set; }
        public static InspectorPane Inspector { get; private set; }
    }
}

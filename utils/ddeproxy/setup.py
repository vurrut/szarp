# py2exe setup file for ddeproxy
# run something like:
#  python setup.py py2exe
#
# If everything works well, you should find a subdirectory named 'dist'
# containing output files.

# modify it to suite your environment
mfcdir = r'C:\Python27\Lib\site-packages\pythonwin\\'

from distutils.core import setup
import py2exe

mfcfiles = map(lambda x: mfcdir + x, ["mfc90.dll", "mfc90u.dll", "mfcm90.dll",
                                      "mfcm90u.dll",
                                      "Microsoft.VC90.MFC.manifest"])

data_files = [ ("Microsoft.VC90.MFC", mfcfiles), ]

 
class Target:
        def __init__(self,**kw):
                        self.__dict__.update(kw)
                        self.version        = "1.00.00"
                        self.compay_name    = "szarp.org"
                        self.copyright      = "(c) 2011, Newterm"

ddeproxy = Target(
    # The first three parameters are not required, if at least a
    # 'version' is given, then a versioninfo resource is built from
    # them and added to the executables.
    data_files = data_files,
    version = "0.2",
    description = "SZARP DDE Proxy",
    name = "ddeproxy",

    # targets to build
    modules = ["ddeproxy"],
    )

setup(service = [ddeproxy])


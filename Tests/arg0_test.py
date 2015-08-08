import codecs
import os
import subprocess
import sys
import tempfile
import unittest
import win32file

class Clone:
    def __init__(self, src, dest):
        self.src = src
        self.dest = dest
    
    def __enter__(self):
        with open(self.src, 'rb') as f1, open(self.dest, 'wb') as f2:
            f2.write(f1.read())
        return self
    
    def __exit__(self, exc_type, exc_value, exc_tb):
        os.unlink(self.dest)
    
    def run(self, *args):
        os.environ["PYLAUNCHER_NOLAUNCH"] = "1"
        os.environ["PYLAUNCHER_VERBOSE"] = "1"
        try:
            out = subprocess.check_output(
                [self.dest] + list(args),
                stderr=subprocess.STDOUT,
            )
        except subprocess.CalledProcessError as ex:
            out = ex.output.decode('utf-8')
            print(out)
            return out.splitlines()
        finally:
            del os.environ["PYLAUNCHER_NOLAUNCH"]
            del os.environ["PYLAUNCHER_VERBOSE"]
        return out.decode('utf-8').splitlines()

class Test_arg0(unittest.TestCase):
    def _clone(self, name):
        return Clone(self._python, os.path.join(os.path.dirname(self._python), name))

    def __init__(self, methodName = 'runTest'):
        super().__init__(methodName)
        self._python = os.path.abspath(os.path.join(os.path.split(__file__)[0], '..', 'Debug', 'python.exe'))
        self.version = "{0[0]}.{0[1]}{1}".format(sys.version_info, '-32' if sys.maxsize < 2**32 else '')

    def test_all(self):
        for suffix in ['2', '2.7', '2.6', '3', '3.4', '3.3']:
            with self.subTest(suffix=suffix):
                with self._clone("python{}.exe".format(suffix)) as c:
                    lines = c.run()
                    self.assertIn("Found version '{}' in process name".format(suffix), lines)
                
                with self._clone("python{}-32.exe".format(suffix)) as c:
                    lines = c.run()
                    self.assertIn("Found version '{}-32' in process name".format(suffix), lines)
                    self.assertIn("Only including 32-bit interpreters", lines)
                
                with self._clone("python{}w.exe".format(suffix)) as c:
                    lines = c.run()
                    self.assertIn("Found version '{}w' in process name".format(suffix), lines)
                    self.assertIn("Preferring windowed interpreters", lines)

if __name__ == '__main__':
    unittest.main()

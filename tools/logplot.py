#!/usr/bin/env python -i

# Script:  logplot.py
# Purpose: use GnuPlot to plot two columns from a SPARTA log file
# Syntax:  logplot.py log.sparta X Y
#          log.sparta = SPARTA log file
#          X,Y = plot Y versus X where X,Y are stats keywords
#          once plot appears, you are in Python interpreter, type C-D to exit
# Author:  Steve Plimpton (Sandia), sjplimp at sandia.gov

import sys,os
path = os.environ["SPARTA_PYTHON_TOOLS"]
sys.path.append(path)
from olog import olog
from gnu import gnu

if len(sys.argv) != 4:
  raise StandardError, "Syntax: logplot.py log.sparta X Y"

logfile = sys.argv[1]
xlabel = sys.argv[2]
ylabel = sys.argv[3]

lg = olog(logfile)
x,y = lg.get(xlabel,ylabel)
g = gnu()
g.plot(x,y)
print "Type Ctrl-D to exit Python"
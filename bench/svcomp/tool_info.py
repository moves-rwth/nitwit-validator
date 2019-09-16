"""
BenchExec is a framework for reliable benchmarking.
This file is part of BenchExec.

Copyright (C) 2007-2015  Dirk Beyer
All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""

# prepare for Python 3
from __future__ import absolute_import, division, print_function, unicode_literals

import logging
import subprocess
import sys
import os
import re

import benchexec.result as result
import benchexec.util as util
import benchexec.tools.template
from benchexec.model import SOFTTIMELIMIT


class Tool(benchexec.tools.template.BaseTool):
	"""
	Tool info for {TODO CWValidator}, an interpreter-based violation witness validator.
	URL:
	"""
	BUILD_PATH = 'cmake-build-release'

	def executable(self):
		executable = util.find_executable('validator.sh')
		build_path = os.path.join(os.path.pardir(executable), self.BUILD_PATH)
		if not os.path.isdir(build_path):
			self._build(build_path)
		if not os.path.isfile(os.path.join(build_path, "cwvalidator32")) or \
				not os.path.isfile(os.path.join(build_path, "cwvalidator64")):
			logging.warning("Required binary files for CWValidator not found in {0}.".format(build_path))
		return executable

	def program_files(self, executable):
		return [executable, os.path.join(self.BUILD_PATH, 'cwvalidator32'),
		        os.path.join(self.BUILD_PATH, 'cwvalidator64')]

	def _build(self, executableDir):
		logging.debug('Building CWValidator in directory {0}.'.format(executableDir))
		build = subprocess.Popen(['build.sh'])
		build.communicate()
		if build.returncode:
			sys.exit('Failed to build cwvalidator, fix it please.')

	def version(self, executable):
		stdout = self._version_from_tool(executable, '--version')
		return stdout.strip()

	def name(self):
		return 'CWValidator'

	def cmdline(self, executable, options, tasks, propertyfile=None, rlimits={}):
		return [executable] + options + tasks

	def determine_result(self, ret_code, returnsignal, output, isTimeout):
		"""
		@param returncode: code returned
		@param returnsignal: signal, which terminated validator
		@param output: the std output
		@return: status of validator after executing a run
		"""

		if ret_code == 0 or ret_code == 245:
			status = result.RESULT_FALSE_REACH
		elif ret_code is None or ret_code == -9:
			status = 'TIMEOUT'
		elif ret_code in [4, 5, 240, 241, 242, 243, 250]:
			status = result.RESULT_UNKNOWN
		else:
			status = result.RESULT_ERROR

		if 'Out of memory' in output:
			status = 'OUT OF MEMORY'

		if not status:
			status = result.RESULT_ERROR

		if isTimeout:
			status = 'TIMEOUT'
		return status

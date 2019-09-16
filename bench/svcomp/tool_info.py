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
	Tool info for {TODO}, the Configurable Software-Verification Platform.
	URL:

	"""

	REQUIRED_PATHS = ['cmake-build-release']

	def executable(self):
		executable = util.find_executable('cwvalidator')
		return executable

	def program_files(self, executable):
		return []

	def _buildCPAchecker(self, executableDir):
		logging.debug('Building CPAchecker in directory {0}.'.format(executableDir))
		cmake = subprocess.Popen(['cmake', '..'], cwd=executableDir,
		                       shell=util.is_windows())
		cmake.communicate()
		if cmake.returncode:
			sys.exit('Failed to build cwvalidator, fix it please.')

	def version(self, executable):
		stdout = self._version_from_tool(executable, '--version')
		line = next(l for l in stdout.splitlines() if l.startswith('CPAchecker'))
		line = line.replace('CPAchecker', '')
		line = line.split('(')[0]
		return line.strip()

	def name(self):
		return 'CWValidator'

	def _get_additional_options(self, existing_options, propertyfile, rlimits):
		return []

	def cmdline(self, executable, options, tasks, propertyfile=None, rlimits={}):
		additional_options = self._get_additional_options(options, propertyfile, rlimits)
		return [executable] + options + additional_options + tasks

	def determine_result(self, ret_code, returnsignal, output, isTimeout):
		"""
		@param returncode: code returned
		@param returnsignal: signal, which terminated validator
		@param output: the std output
		@return: status of validator after executing a run
		"""

		if ret_code == 0 or ret_code == 245:
			status = result.RESULT_FALSE_REACH
		elif ret_code is None or ret_code == -9 or isTimeout:
			status = 'TIMEOUT'
		elif ret_code in [4, 5, 240, 241, 242, 243, 250]:
			status = result.RESULT_UNKNOWN
		else:
			status = result.RESULT_ERROR

		if 'Out of memory' in output:
			status = 'OUT OF MEMORY'

		if not status:
			status = result.RESULT_ERROR
		return status

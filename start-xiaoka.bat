@echo off
set "ROOT=%~dp0"
cd /d "%ROOT%"
set XIAOKA_USE_QWEN=1
if not defined XIAOKA_PYTHON set XIAOKA_PYTHON=python
if not defined XIAOKA_QWEN_MODEL set "XIAOKA_QWEN_MODEL=%ROOT%..\Models\Qwen3-0.6B"
npm start

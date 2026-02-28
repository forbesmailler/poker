# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

poker

## Dependencies

Managed via `environment.yaml` (mamba/conda).

## Development

```bash
invoke format   # ruff format + check
invoke test     # pytest with coverage
invoke all      # both
```

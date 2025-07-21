"""Logging configuration for cppq CLI."""

import logging
import sys
from pathlib import Path
from typing import Optional
from rich.logging import RichHandler


def setup_logging(
    debug: bool = False, log_file: Optional[Path] = None
) -> logging.Logger:
    """Configure logging for the CLI application.

    Args:
        debug: Enable debug level logging
        log_file: Optional path to log file

    Returns:
        Configured logger instance
    """
    logger = logging.getLogger("cppq-cli")
    logger.setLevel(logging.DEBUG if debug else logging.INFO)

    # Remove any existing handlers
    logger.handlers.clear()

    # Console handler with Rich formatting
    console_handler = RichHandler(
        rich_tracebacks=True,
        tracebacks_show_locals=debug,
        show_path=debug,
        show_time=debug,
    )
    console_handler.setLevel(logging.DEBUG if debug else logging.WARNING)
    console_formatter = logging.Formatter("%(message)s")
    console_handler.setFormatter(console_formatter)
    logger.addHandler(console_handler)

    # File handler if log file is specified
    if log_file:
        try:
            log_file.parent.mkdir(parents=True, exist_ok=True)
            file_handler = logging.FileHandler(log_file)
            file_handler.setLevel(logging.DEBUG)
            file_formatter = logging.Formatter(
                "%(asctime)s - %(name)s - %(levelname)s - %(message)s",
                datefmt="%Y-%m-%d %H:%M:%S",
            )
            file_handler.setFormatter(file_formatter)
            logger.addHandler(file_handler)
        except Exception as e:
            logger.error(f"Failed to create log file handler: {e}")

    return logger


def get_logger(name: Optional[str] = None) -> logging.Logger:
    """Get a logger instance.

    Args:
        name: Optional logger name (will be prefixed with 'cppq-cli.')

    Returns:
        Logger instance
    """
    if name:
        return logging.getLogger(f"cppq-cli.{name}")
    return logging.getLogger("cppq-cli")

"""Configuration management for cppq CLI."""

import os
import json
from pathlib import Path
from typing import Dict, Any, Optional
from pydantic import BaseModel, Field, validator
from dotenv import load_dotenv


class CLIConfig(BaseModel):
    """Configuration model for cppq CLI."""

    redis_uri: str = Field(
        default="redis://localhost", description="Redis connection URI"
    )
    output_format: str = Field(
        default="table", description="Default output format (table, json, pretty)"
    )
    debug: bool = Field(default=False, description="Enable debug logging")
    log_file: Optional[Path] = Field(default=None, description="Log file path")

    @validator("output_format")
    def validate_output_format(cls, v):
        allowed = ["table", "json", "pretty"]
        if v not in allowed:
            raise ValueError(f"output_format must be one of {allowed}")
        return v

    @validator("log_file", pre=True)
    def validate_log_file(cls, v):
        if v:
            return Path(v)
        return None


class ConfigManager:
    """Manages configuration loading from multiple sources."""

    def __init__(self, config_file: Optional[Path] = None):
        self.config_file = config_file or self._get_default_config_path()
        self._config: Optional[CLIConfig] = None

    @staticmethod
    def _get_default_config_path() -> Path:
        """Get the default configuration file path."""
        # Priority order:
        # 1. $CPPQ_CONFIG_FILE
        # 2. ./cppq.json
        # 3. ~/.config/cppq/config.json
        # 4. /etc/cppq/config.json

        if env_path := os.getenv("CPPQ_CONFIG_FILE"):
            return Path(env_path)

        paths = [
            Path("./cppq.json"),
            Path.home() / ".config" / "cppq" / "config.json",
            Path("/etc/cppq/config.json"),
        ]

        for path in paths:
            if path.exists():
                return path

        # Return user config path as default (even if it doesn't exist)
        return Path.home() / ".config" / "cppq" / "config.json"

    def load_config(self) -> CLIConfig:
        """Load configuration from all sources with proper precedence."""
        if self._config is not None:
            return self._config

        # Start with defaults
        config_data = {}

        # Load from config file if it exists
        if self.config_file.exists():
            try:
                with open(self.config_file, "r") as f:
                    file_config = json.load(f)
                    config_data.update(file_config)
            except Exception as e:
                print(f"Warning: Failed to load config file: {e}")

        # Override with environment variables
        load_dotenv()

        env_mappings = {
            "REDIS_URI": "redis_uri",
            "CPPQ_OUTPUT_FORMAT": "output_format",
            "CPPQ_DEBUG": "debug",
            "CPPQ_LOG_FILE": "log_file",
        }

        for env_var, config_key in env_mappings.items():
            if value := os.getenv(env_var):
                if config_key == "debug":
                    config_data[config_key] = value.lower() in (
                        "true",
                        "1",
                        "yes",
                        "on",
                    )
                else:
                    config_data[config_key] = value

        self._config = CLIConfig(**config_data)
        return self._config

    def save_config(self, config: CLIConfig):
        """Save configuration to file."""
        self.config_file.parent.mkdir(parents=True, exist_ok=True)

        config_dict = config.dict(exclude_none=True)
        # Convert Path to string for JSON serialization
        if "log_file" in config_dict and config_dict["log_file"]:
            config_dict["log_file"] = str(config_dict["log_file"])

        with open(self.config_file, "w") as f:
            json.dump(config_dict, f, indent=2)

    def create_default_config(self):
        """Create a default configuration file."""
        default_config = CLIConfig()
        self.save_config(default_config)
        return self.config_file

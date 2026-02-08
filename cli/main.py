#!/usr/bin/env python3
"""
cppq CLI - A modern Redis queue management command-line interface.
"""

import sys
import json
from typing import Dict, List, Any, Optional, Tuple
from pathlib import Path

import click
import redis
from rich.console import Console
from rich.table import Table
from rich import print as rprint
from pydantic import BaseModel, Field
from dotenv import load_dotenv

from config import ConfigManager, CLIConfig
from logger import setup_logging, get_logger

load_dotenv()

console = Console()
logger = get_logger()


class QueueInfo(BaseModel):
    """Model for queue information."""

    name: str
    priority: int
    paused: bool


class QueueStats(BaseModel):
    """Model for queue statistics."""

    pending: int = Field(ge=0)
    scheduled: int = Field(ge=0)
    active: int = Field(ge=0)
    completed: int = Field(ge=0)
    failed: int = Field(ge=0)

    @property
    def total(self) -> int:
        return (
            self.pending + self.scheduled + self.active + self.completed + self.failed
        )


class CppqCLI:
    """Main CLI handler for cppq operations."""

    def __init__(self, redis_uri: str):
        self.redis_uri = redis_uri
        self._client: Optional[redis.Redis] = None

    @property
    def client(self) -> redis.Redis:
        """Lazy Redis client initialization with connection validation."""
        if self._client is None:
            try:
                self._client = redis.Redis.from_url(
                    self.redis_uri, decode_responses=False
                )
                self._client.ping()
            except redis.ConnectionError as e:
                console.print(f"[red]✗ Failed to connect to Redis: {e}[/red]")
                sys.exit(1)
            except Exception as e:
                logger.exception("Failed to connect to Redis")
                console.print(f"[red]✗ Unexpected error: {e}[/red]")
                sys.exit(1)
        return self._client

    def decode_redis_value(self, value: Any) -> Any:
        """Recursively decode Redis values from bytes to strings."""
        if isinstance(value, bytes):
            return value.decode("utf-8")
        elif isinstance(value, list):
            return [self.decode_redis_value(item) for item in value]
        elif isinstance(value, dict):
            return {
                self.decode_redis_value(k): self.decode_redis_value(v)
                for k, v in value.items()
            }
        return value

    @staticmethod
    def parse_queue_entry(queue_str: str) -> Tuple[str, int]:
        """Parse a queue entry like 'name:10' into (name, priority)."""
        name, sep, suffix = queue_str.rpartition(":")
        if sep and suffix.isdigit():
            return name, int(suffix)
        return queue_str, 0

    def get_queues(self) -> List[QueueInfo]:
        """Retrieve all queues with their metadata."""
        queue_keys = self.client.smembers("cppq:queues")
        queues = []

        for queue_key in queue_keys:
            queue_str = queue_key.decode("utf-8")
            name, priority = self.parse_queue_entry(queue_str)
            paused = self.client.sismember("cppq:queues:paused", name)

            queues.append(QueueInfo(name=name, priority=priority, paused=bool(paused)))

        return sorted(queues, key=lambda q: (-q.priority, q.name))

    def get_queue_stats(self, queue_name: str) -> QueueStats:
        """Get statistics for a specific queue."""
        states = ["pending", "scheduled", "active", "completed", "failed"]
        stats = {}

        for state in states:
            key = f"cppq:{queue_name}:{state}"
            count = self.client.llen(key)
            stats[state] = count

        return QueueStats(**stats)

    def list_tasks(self, queue_name: str, state: str) -> List[str]:
        """List task UUIDs in a specific queue state."""
        key = f"cppq:{queue_name}:{state}"
        task_uuids = self.client.lrange(key, 0, -1)
        return [uuid.decode("utf-8") for uuid in task_uuids]

    def get_task_details(self, queue_name: str, task_uuid: str) -> Dict[str, Any]:
        """Get detailed information about a specific task."""
        key = f"cppq:{queue_name}:task:{task_uuid}"
        task_data = self.client.hgetall(key)

        if not task_data:
            raise ValueError(f"Task {task_uuid} not found in queue {queue_name}")

        return self.decode_redis_value(task_data)

    def pause_queue(self, queue_name: str) -> bool:
        """Pause a queue."""
        result = self.client.sadd("cppq:queues:paused", queue_name)
        return bool(result)

    def unpause_queue(self, queue_name: str) -> bool:
        """Unpause a queue."""
        result = self.client.srem("cppq:queues:paused", queue_name)
        return bool(result)


pass_cli = click.make_pass_decorator(CppqCLI)


@click.group(invoke_without_command=True)
@click.option(
    "--redis-uri",
    envvar="REDIS_URI",
    help="Redis connection URI (can also be set via REDIS_URI env var)",
)
@click.option(
    "--format",
    type=click.Choice(["table", "json", "pretty"], case_sensitive=False),
    help="Output format",
)
@click.option(
    "--config",
    type=click.Path(exists=False, path_type=Path),
    help="Path to configuration file",
)
@click.option("--debug", is_flag=True, help="Enable debug logging")
@click.pass_context
def cli(
    ctx: click.Context,
    redis_uri: Optional[str],
    format: Optional[str],
    config: Optional[Path],
    debug: bool,
):
    """cppq CLI - Redis Queue Management Tool

    A modern command-line interface for managing cppq (C++ Queue) Redis queues.
    """
    ctx.ensure_object(dict)

    # Load configuration
    config_manager = ConfigManager(config)
    cli_config = config_manager.load_config()

    # Override with command line options
    if redis_uri:
        cli_config.redis_uri = redis_uri
    if format:
        cli_config.output_format = format
    if debug:
        cli_config.debug = debug

    # Setup logging
    setup_logging(cli_config.debug, cli_config.log_file)
    logger.debug(f"Loaded configuration: {cli_config}")

    ctx.obj["cli"] = CppqCLI(cli_config.redis_uri)
    ctx.obj["format"] = cli_config.output_format
    ctx.obj["config"] = cli_config

    if ctx.invoked_subcommand is None:
        click.echo(ctx.get_help())


@cli.command()
@click.pass_context
def queues(ctx: click.Context):
    """List all queues with their priorities and pause status."""
    cppq_cli: CppqCLI = ctx.obj["cli"]
    format_type = ctx.obj["format"]

    try:
        queues = cppq_cli.get_queues()

        if format_type == "json":
            data = [q.dict() for q in queues]
            console.print_json(json.dumps(data))
        elif format_type == "pretty":
            for q in queues:
                rprint(q.dict())
        else:  # table
            table = Table(
                title="cppq Queues", show_header=True, header_style="bold cyan"
            )
            table.add_column("Queue Name", style="bright_blue")
            table.add_column("Priority", justify="center")
            table.add_column("Status", justify="center")

            for q in queues:
                status = "[red]Paused[/red]" if q.paused else "[green]Active[/green]"
                table.add_row(q.name, q.priority, status)

            console.print(table)

    except Exception as e:
        logger.error(f"Error in queues command: {e}", exc_info=True)
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command()
@click.argument("queue")
@click.pass_context
def stats(ctx: click.Context, queue: str):
    """Display statistics for a specific queue."""
    cppq_cli: CppqCLI = ctx.obj["cli"]
    format_type = ctx.obj["format"]

    try:
        stats = cppq_cli.get_queue_stats(queue)

        if format_type == "json":
            console.print_json(stats.json())
        elif format_type == "pretty":
            rprint(stats.dict())
        else:  # table
            table = Table(
                title=f"Queue Statistics: {queue}",
                show_header=True,
                header_style="bold cyan",
            )
            table.add_column("State", style="bright_blue")
            table.add_column("Count", justify="right")

            for state, count in stats.dict().items():
                if state != "total":
                    style = {
                        "pending": "yellow",
                        "scheduled": "cyan",
                        "active": "green",
                        "completed": "blue",
                        "failed": "red",
                    }.get(state, "white")
                    table.add_row(state.capitalize(), f"[{style}]{count}[/{style}]")

            table.add_section()
            table.add_row("Total", f"[bold]{stats.total}[/bold]")

            console.print(table)

    except Exception as e:
        logger.error(f"Error in stats command: {e}", exc_info=True)
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command(name="list")
@click.argument("queue")
@click.argument(
    "state",
    type=click.Choice(["pending", "scheduled", "active", "completed", "failed"]),
)
@click.option("--limit", "-n", type=int, help="Limit number of results")
@click.pass_context
def list_tasks_cmd(ctx: click.Context, queue: str, state: str, limit: Optional[int]):
    """List task UUIDs in a specific queue state."""
    cppq_cli: CppqCLI = ctx.obj["cli"]
    format_type = ctx.obj["format"]

    try:
        task_uuids = cppq_cli.list_tasks(queue, state)

        if limit:
            task_uuids = task_uuids[:limit]

        if format_type == "json":
            console.print_json(json.dumps(task_uuids))
        elif format_type == "pretty":
            rprint(task_uuids)
        else:  # table
            if not task_uuids:
                console.print(f"[yellow]No tasks found in {queue}:{state}[/yellow]")
                return

            table = Table(
                title=f"Tasks in {queue}:{state}",
                show_header=True,
                header_style="bold cyan",
            )
            table.add_column("Index", justify="right", style="dim")
            table.add_column("Task UUID", style="bright_blue")

            for idx, uuid in enumerate(task_uuids, 1):
                table.add_row(str(idx), uuid)

            console.print(table)

            if limit and len(task_uuids) == limit:
                console.print(f"\n[dim]Showing first {limit} results[/dim]")

    except Exception as e:
        logger.error(f"Error in list command: {e}", exc_info=True)
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command()
@click.argument("queue")
@click.argument("uuid")
@click.pass_context
def task(ctx: click.Context, queue: str, uuid: str):
    """Get detailed information about a specific task."""
    cppq_cli: CppqCLI = ctx.obj["cli"]
    format_type = ctx.obj["format"]

    try:
        task_details = cppq_cli.get_task_details(queue, uuid)

        if format_type == "json":
            console.print_json(json.dumps(task_details))
        elif format_type == "pretty":
            rprint(task_details)
        else:  # table
            table = Table(
                title=f"Task Details: {uuid}",
                show_header=True,
                header_style="bold cyan",
            )
            table.add_column("Field", style="bright_blue")
            table.add_column("Value")

            for key, value in task_details.items():
                # Format value for better display
                if isinstance(value, (dict, list)):
                    value = json.dumps(value, indent=2)
                table.add_row(key, str(value))

            console.print(table)

    except ValueError as e:
        logger.warning(f"Task not found: {e}")
        console.print(f"[yellow]Warning: {e}[/yellow]")
        sys.exit(1)
    except Exception as e:
        logger.error(f"Error in task command: {e}", exc_info=True)
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command()
@click.argument("queue")
@click.pass_context
def pause(ctx: click.Context, queue: str):
    """Pause a queue."""
    cppq_cli: CppqCLI = ctx.obj["cli"]

    try:
        result = cppq_cli.pause_queue(queue)
        if result:
            console.print(f"[green]✓ Queue '{queue}' has been paused[/green]")
        else:
            console.print(f"[yellow]Queue '{queue}' was already paused[/yellow]")
        logger.info(f"Paused queue: {queue}")
    except Exception as e:
        logger.error(f"Error pausing queue: {e}", exc_info=True)
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command()
@click.argument("queue")
@click.pass_context
def unpause(ctx: click.Context, queue: str):
    """Unpause a queue."""
    cppq_cli: CppqCLI = ctx.obj["cli"]

    try:
        result = cppq_cli.unpause_queue(queue)
        if result:
            console.print(f"[green]✓ Queue '{queue}' has been unpaused[/green]")
        else:
            console.print(f"[yellow]Queue '{queue}' was not paused[/yellow]")
        logger.info(f"Unpaused queue: {queue}")
    except Exception as e:
        logger.error(f"Error unpausing queue: {e}", exc_info=True)
        console.print(f"[red]Error: {e}[/red]")
        sys.exit(1)


@cli.command()
@click.pass_context
def version(ctx: click.Context):
    """Show version information."""
    console.print("[bold]cppq CLI[/bold] version 2.0.0")
    console.print("A modern Redis queue management tool")


@cli.command()
@click.option("--create", is_flag=True, help="Create default configuration file")
@click.pass_context
def config(ctx: click.Context, create: bool):
    """Manage CLI configuration."""
    if create:
        config_manager = ConfigManager()
        config_path = config_manager.create_default_config()
        console.print(
            f"[green]✓ Created default configuration at: {config_path}[/green]"
        )
    else:
        cli_config: CLIConfig = ctx.obj["config"]
        console.print("[bold]Current Configuration:[/bold]")
        for field, value in cli_config.dict().items():
            console.print(f"  {field}: {value}")


if __name__ == "__main__":
    cli()

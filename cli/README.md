# cppq CLI - Modern Redis Queue Management Tool

A powerful, modern command-line interface for managing cppq (C++ Queue) Redis queues with rich formatting, comprehensive logging, and flexible configuration options.

## Features

- **Modern CLI Framework**: Built with Click for intuitive command structure
- **Rich Output Formatting**: Beautiful tables, JSON, and pretty-print output options
- **Type Safety**: Full type hints and Pydantic models for data validation
- **Comprehensive Error Handling**: User-friendly error messages with detailed logging
- **Flexible Configuration**: Support for config files, environment variables, and command-line options
- **Colorized Output**: Enhanced readability with Rich formatting
- **Debug Mode**: Detailed logging for troubleshooting

## Installation

1. Clone the repository and navigate to the CLI directory:
```bash
cd cppq/cli
```

2. Install dependencies:
```bash
pip install -r requirements.txt
```

3. Make the CLI executable (optional):
```bash
chmod +x main.py
```

## Configuration

The CLI supports multiple configuration methods with the following precedence:
1. Command-line arguments (highest priority)
2. Environment variables
3. Configuration file
4. Default values (lowest priority)

### Environment Variables

Copy `.env.example` to `.env` and customize:
```bash
cp .env.example .env
```

Available environment variables:
- `REDIS_URI`: Redis connection URI (default: `redis://localhost`)
- `CPPQ_OUTPUT_FORMAT`: Default output format (table/json/pretty)
- `CPPQ_DEBUG`: Enable debug logging (true/false)
- `CPPQ_LOG_FILE`: Path to log file (optional)

### Configuration File

Create a configuration file:
```bash
python main.py config --create
```

This creates a config file at `~/.config/cppq/config.json`:
```json
{
  "redis_uri": "redis://localhost",
  "output_format": "table",
  "debug": false,
  "log_file": null
}
```

## Usage

### Basic Commands

Show help:
```bash
python main.py --help
```

List all queues:
```bash
python main.py queues
```

Get queue statistics:
```bash
python main.py stats myqueue
```

List tasks in a queue:
```bash
python main.py list myqueue pending
python main.py list myqueue active --limit 10
```

Get task details:
```bash
python main.py task myqueue 123e4567-e89b-12d3-a456-426614174000
```

Pause/unpause a queue:
```bash
python main.py pause myqueue
python main.py unpause myqueue
```

### Output Formats

Use different output formats with the `--format` option:

Table format (default):
```bash
python main.py queues --format table
```

JSON format:
```bash
python main.py queues --format json
```

Pretty-print format:
```bash
python main.py queues --format pretty
```

### Advanced Options

Enable debug logging:
```bash
python main.py --debug queues
```

Use custom Redis URI:
```bash
python main.py --redis-uri redis://myserver:6379/0 queues
```

Use custom config file:
```bash
python main.py --config /path/to/config.json queues
```

## Command Reference

### `queues`
List all queues with their priorities and pause status.

```bash
python main.py queues
```

### `stats <queue>`
Display statistics for a specific queue.

```bash
python main.py stats myqueue
```

### `list <queue> <state>`
List task UUIDs in a specific queue state.

States: `pending`, `scheduled`, `active`, `completed`, `failed`

```bash
python main.py list myqueue pending
python main.py list myqueue active --limit 20
```

### `task <queue> <uuid>`
Get detailed information about a specific task.

```bash
python main.py task myqueue 123e4567-e89b-12d3-a456-426614174000
```

### `pause <queue>`
Pause a queue to prevent task processing.

```bash
python main.py pause myqueue
```

### `unpause <queue>`
Unpause a queue to resume task processing.

```bash
python main.py unpause myqueue
```

### `config`
Manage CLI configuration.

```bash
# Show current configuration
python main.py config

# Create default configuration file
python main.py config --create
```

### `version`
Show version information.

```bash
python main.py version
```

## Examples

### Example 1: Monitor Queue Health
```bash
# Check all queues
python main.py queues

# Get detailed stats for problematic queue
python main.py stats problem-queue

# List failed tasks
python main.py list problem-queue failed --format json | jq '.'
```

### Example 2: Debug Task Issues
```bash
# Enable debug mode and check task details
python main.py --debug task myqueue 123e4567-e89b-12d3-a456-426614174000
```

### Example 3: Batch Operations
```bash
# Export all pending tasks to JSON
python main.py list myqueue pending --format json > pending-tasks.json

# Pause multiple queues
for queue in queue1 queue2 queue3; do
    python main.py pause $queue
done
```

## Logging

Logs are written to:
- Console (warnings and above by default, all levels in debug mode)
- File (if configured via `CPPQ_LOG_FILE` or config file)

Enable debug logging:
```bash
python main.py --debug <command>
```

Configure log file:
```bash
export CPPQ_LOG_FILE=/var/log/cppq-cli.log
```

## Troubleshooting

### Connection Issues
If you can't connect to Redis:
1. Check Redis is running: `redis-cli ping`
2. Verify connection URI: `python main.py --redis-uri redis://localhost:6379 queues`
3. Enable debug mode: `python main.py --debug queues`

### Permission Issues
If you get permission errors:
1. Check Redis ACL/password requirements
2. Use authenticated URI: `redis://username:password@host:port`

### Performance Issues
For better performance with large queues:
1. Use `--limit` flag when listing tasks
2. Use JSON output format for programmatic processing
3. Consider using Redis connection pooling

## Development

### Code Structure
- `main.py`: Main CLI entry point with command definitions
- `config.py`: Configuration management
- `logger.py`: Logging setup and utilities
- `requirements.txt`: Python dependencies

### Adding New Commands
1. Add command function to `main.py`
2. Use `@cli.command()` decorator
3. Add proper type hints and error handling
4. Update this README

### Running Tests
```bash
# Run tests (when implemented)
pytest tests/
```

## License

See the main project LICENSE file.

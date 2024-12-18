import redis
import sys
import argparse


def decode_redis(src):
    if isinstance(src, list):
        rv = list()
        for key in src:
            rv.append(decode_redis(key))
        return rv
    elif isinstance(src, dict):
        rv = dict()
        for key in src:
            rv[key.decode()] = decode_redis(src[key])
        return rv
    elif isinstance(src, bytes):
        return src.decode()
    else:
        raise Exception("type not handled: " + type(src))


def main():
    parser = argparse.ArgumentParser(description='cppq CLI')
    parser.add_argument('--redis_uri', dest='redis_uri', default='redis://localhost')
    parser.add_argument('--queues', dest='queues', action='store_true', help='print queues, priorities, and pause status ')
    parser.add_argument('--stats', dest='stats', metavar=('QUEUE'), help='print queue statistics')
    parser.add_argument('--list', type=str, nargs=2, help='list task UUIDs in queue', metavar=('QUEUE', 'STATE'))
    parser.add_argument('--task', type=str, nargs=2, help='get task details', metavar=('QUEUE', 'UUID'))

    if len(sys.argv) == 1:
        parser.print_help(sys.stderr)
        return

    args = parser.parse_args()

    try:
        redisClient = redis.Redis.from_url(args.redis_uri)
        redisClient.ping()
    except Exception as e:
        return 'Failed to connect to Redis: ' + str(e)

    if args.queues:
        queues = [x.decode('ascii') for x in list(redisClient.smembers('cppq:queues'))]
        result = {}
        for queue in queues:
            name = queue.split(':')[0];
            paused = redisClient.sismember('cppq:queues:paused', name)
            result[name] = { 'priority': queue.split(':')[1], 'paused': paused }
        return result

    if args.stats:
        pending = redisClient.llen('cppq:' + args.stats + ':pending')
        scheduled = redisClient.llen('cppq:' + args.stats + ':scheduled')
        active = redisClient.llen('cppq:' + args.stats + ':active')
        completed = redisClient.llen('cppq:' + args.stats + ':completed')
        failed = redisClient.llen('cppq:' + args.stats + ':failed')
        return { 'pending': pending, 'scheduled': scheduled, 'active': active, 'completed': completed, 'failed': failed }

    if args.list:
        queue, state = args.list
        taskUuids = [x.decode('ascii') for x in redisClient.lrange('cppq:' + queue + ':' + state, 0, -1)]
        return taskUuids

    if args.task:
        queue, uuid = args.task
        return decode_redis(redisClient.hgetall('cppq:' + queue + ':task:' + uuid))


if __name__ == '__main__':
    print(main())

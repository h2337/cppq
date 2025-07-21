/**
 * Extracts the base queue name from a queue name that may include a priority suffix
 * e.g., "high:20" -> "high", "default:10" -> "default"
 */
export function getBaseQueueName(queueName: string): string {
  const colonIndex = queueName.lastIndexOf(':');
  if (colonIndex === -1) return queueName;
  
  // Check if what follows the colon is a number (priority)
  const suffix = queueName.substring(colonIndex + 1);
  if (/^\d+$/.test(suffix)) {
    return queueName.substring(0, colonIndex);
  }
  
  // If not a number, return the full name
  return queueName;
}

/**
 * Extracts the priority from a queue name
 * e.g., "high:20" -> 20, "default:10" -> 10
 */
export function getQueuePriority(queueName: string): number {
  const colonIndex = queueName.lastIndexOf(':');
  if (colonIndex === -1) return 0;
  
  const suffix = queueName.substring(colonIndex + 1);
  const priority = parseInt(suffix, 10);
  
  return isNaN(priority) ? 0 : priority;
}
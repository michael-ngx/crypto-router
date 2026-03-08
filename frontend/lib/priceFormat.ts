type DynamicPriceOptions = {
  minNonZeroDigits?: number;
  maxFractionDigits?: number;
};

export function formatDynamicPrice(
  value: number,
  options: DynamicPriceOptions = {}
): string {
  if (!Number.isFinite(value)) return "N/A";

  const minNonZeroDigits = options.minNonZeroDigits ?? 3;
  const maxFractionDigits = options.maxFractionDigits ?? 14;
  const abs = Math.abs(value);

  if (abs === 0) {
    return "0.00";
  }

  if (abs >= 1) {
    return value.toLocaleString(undefined, {
      minimumFractionDigits: 2,
      maximumFractionDigits: 3,
    });
  }

  // For sub-1 prices, reveal enough trailing precision to surface non-zero digits.
  const fractional = abs.toFixed(18).split(".")[1] ?? "";
  const firstNonZero = fractional.search(/[1-9]/);
  if (firstNonZero < 0) {
    return "0.00";
  }

  const fractionDigits = Math.min(
    maxFractionDigits,
    Math.max(2, firstNonZero + minNonZeroDigits)
  );

  return value.toLocaleString(undefined, {
    minimumFractionDigits: fractionDigits,
    maximumFractionDigits: fractionDigits,
  });
}

export function truncateDisplay(text: string, maxChars: number): string {
  if (text.length <= maxChars) return text;
  if (maxChars <= 3) return text.slice(0, Math.max(0, maxChars));
  return `${text.slice(0, maxChars - 3)}...`;
}

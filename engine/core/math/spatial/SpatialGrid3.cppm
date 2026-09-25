module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

export module Engine.Core.Math.SpatialGrid3;

export import Engine.Core.Math.AxisAlignedBox3;

export namespace Engine::Math
{
// A sparse uniform grid for point queries against axis-aligned object bounds.
// Oversized bounds use a separate ordered list instead of occupying an
// unbounded number of cells.
template<class T>
class SpatialGrid3 final
{
public:
	explicit SpatialGrid3(float cell_size = 100.0f) noexcept
		: cell_size_(Is_Usable_Cell_Size(cell_size) ? cell_size : 100.0f) {}

	bool Insert(T *object, AxisAlignedBox3 bounds)
	{
		if (object == nullptr || !bounds.Is_Valid() || !Is_Finite(bounds)) return false;
		if (records_.contains(object)) return Update(object, bounds);
		Record record{bounds, {}, false, next_sequence_++};
		Index(record, object);
		records_.emplace(object, std::move(record));
		order_.push_back(object);
		return true;
	}

	bool Update(T *object, AxisAlignedBox3 bounds)
	{
		const auto found = records_.find(object);
		if (found == records_.end()) return false;
		if (!bounds.Is_Valid() || !Is_Finite(bounds)) return Remove(object);
		Remove_From_Index(object, found->second);
		found->second.bounds = bounds;
		Index(found->second, object);
		return true;
	}

	bool Remove(T *object)
	{
		const auto found = records_.find(object);
		if (found == records_.end()) return false;
		Remove_From_Index(object, found->second);
		records_.erase(found);
		order_.erase(std::remove(order_.begin(), order_.end(), object), order_.end());
		return true;
	}

	void Set_Cell_Size(float cell_size)
	{
		if (!Is_Usable_Cell_Size(cell_size) || cell_size == cell_size_) return;
		cell_size_ = cell_size;
		cells_.clear();
		large_objects_.clear();
		for (T *object : order_) {
			auto &record = records_.at(object);
			record.cells.clear();
			record.is_large = false;
			Index(record, object);
		}
	}

	void Clear() noexcept
	{
		cells_.clear();
		large_objects_.clear();
		records_.clear();
		order_.clear();
		next_sequence_ = 0;
	}

	std::vector<T *> Query_Point(Vector3 point) const
	{
		std::vector<T *> result;
		if (!Is_Finite(point)) return result;
		const Cell cell = Cell_At(point);
		if (const auto found = cells_.find(cell); found != cells_.end())
			result.insert(result.end(), found->second.begin(), found->second.end());
		result.insert(result.end(), large_objects_.begin(), large_objects_.end());
		std::sort(result.begin(), result.end(), [this](T *left, T *right) {
			return records_.at(left).sequence < records_.at(right).sequence;
		});
		return result;
	}

	std::size_t Size() const noexcept { return records_.size(); }
	bool Contains(const T *object) const noexcept { return records_.contains(const_cast<T *>(object)); }

private:
	struct Cell final
	{
		std::int64_t x, y, z;
		friend bool operator==(const Cell &, const Cell &) noexcept = default;
	};

	struct Cell_Hash final
	{
		std::size_t operator()(Cell cell) const noexcept
		{
			std::size_t value = std::hash<std::int64_t>{}(cell.x);
			value ^= std::hash<std::int64_t>{}(cell.y) + std::size_t{0x9e3779b9} + (value << 6) + (value >> 2);
			value ^= std::hash<std::int64_t>{}(cell.z) + std::size_t{0x9e3779b9} + (value << 6) + (value >> 2);
			return value;
		}
	};

	struct Record final
	{
		AxisAlignedBox3 bounds;
		std::vector<Cell> cells;
		bool is_large;
		std::uint64_t sequence;
	};

	static constexpr std::size_t Maximum_Cells_Per_Object = 2048;
	static constexpr std::int64_t Maximum_Cells_Per_Axis = 2048;

	static bool Is_Usable_Cell_Size(float value) noexcept
	{
		return std::isfinite(value) && value > 0.0f;
	}

	static bool Is_Finite(Vector3 point) noexcept
	{
		return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
	}

	static bool Is_Finite(const AxisAlignedBox3 &bounds) noexcept
	{
		return Is_Finite(bounds.minimum) && Is_Finite(bounds.maximum);
	}

	std::int64_t Cell_Axis(float coordinate) const noexcept
	{
		const double index = std::floor(static_cast<double>(coordinate) / cell_size_);
		if (index <= static_cast<double>((std::numeric_limits<std::int64_t>::min)()))
			return (std::numeric_limits<std::int64_t>::min)();
		if (index >= static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
			return (std::numeric_limits<std::int64_t>::max)();
		return static_cast<std::int64_t>(index);
	}

	Cell Cell_At(Vector3 point) const noexcept
	{
		return {Cell_Axis(point.x), Cell_Axis(point.y), Cell_Axis(point.z)};
	}

	void Index(Record &record, T *object)
	{
		const Cell low = Cell_At(record.bounds.minimum);
		const Cell high = Cell_At(record.bounds.maximum);
		const auto span = [](std::int64_t minimum, std::int64_t maximum) {
			if (minimum > maximum)
				return Maximum_Cells_Per_Axis + 1;
			const auto difference = static_cast<std::uint64_t>(maximum)
				- static_cast<std::uint64_t>(minimum);
			if (difference >= static_cast<std::uint64_t>(Maximum_Cells_Per_Axis))
				return Maximum_Cells_Per_Axis + 1;
			return static_cast<std::int64_t>(difference) + 1;
		};
		const std::int64_t x_count = span(low.x, high.x);
		const std::int64_t y_count = span(low.y, high.y);
		const std::int64_t z_count = span(low.z, high.z);
		if (x_count > Maximum_Cells_Per_Axis || y_count > Maximum_Cells_Per_Axis
			|| z_count > Maximum_Cells_Per_Axis
			|| static_cast<std::uint64_t>(x_count) * static_cast<std::uint64_t>(y_count)
				> Maximum_Cells_Per_Object / static_cast<std::uint64_t>(z_count)) {
			record.is_large = true;
			large_objects_.push_back(object);
			return;
		}
		record.is_large = false;
		for (std::int64_t x = low.x;; ++x) {
			for (std::int64_t y = low.y;; ++y) {
				for (std::int64_t z = low.z;; ++z) {
					const Cell cell{x, y, z};
					cells_[cell].push_back(object);
					record.cells.push_back(cell);
					if (z == high.z) break;
				}
				if (y == high.y) break;
			}
			if (x == high.x) break;
		}
	}

	void Remove_From_Index(T *object, const Record &record)
	{
		if (record.is_large) {
			large_objects_.erase(std::remove(large_objects_.begin(), large_objects_.end(), object), large_objects_.end());
			return;
		}
		for (const Cell cell : record.cells) {
			auto found = cells_.find(cell);
			if (found == cells_.end()) continue;
			auto &objects = found->second;
			objects.erase(std::remove(objects.begin(), objects.end(), object), objects.end());
			if (objects.empty()) cells_.erase(found);
		}
	}

	float cell_size_;
	std::uint64_t next_sequence_ = 0;
	std::unordered_map<Cell, std::vector<T *>, Cell_Hash> cells_;
	std::unordered_map<T *, Record> records_;
	std::vector<T *> large_objects_;
	std::vector<T *> order_;
};
}

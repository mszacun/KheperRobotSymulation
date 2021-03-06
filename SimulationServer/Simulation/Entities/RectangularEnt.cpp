#include "RectangularEnt.h"

RectangularEnt::RectangularEnt(uint16_t id, uint32_t weight, bool movable, double x,
	double y, double width, double height, float angle) : SimEnt(id, SimEnt::RECTANGLE, weight, movable),
	_width(width), _height(height), _angle(angle)
{
    initializeEntity(x, y);
}

RectangularEnt::RectangularEnt(std::ifstream& file, bool readBinary) : SimEnt(file, readBinary, SimEnt::RECTANGLE)
{
    double x, y;
    if (readBinary)
    {
        file.read(reinterpret_cast<char*>(&x), sizeof(x));
        file.read(reinterpret_cast<char*>(&y), sizeof(y));
        file.read(reinterpret_cast<char*>(&_width), sizeof(_width));
        file.read(reinterpret_cast<char*>(&_height), sizeof(_height));
        file.read(reinterpret_cast<char*>(&_angle), sizeof(_angle));
    }
    else
        file >> x >> y >> _width >> _height >> _angle;
    initializeEntity(x, y);
}

RectangularEnt::RectangularEnt(const RectangularEnt& other) : SimEnt(other)
{
    _bottLeft = new Point(*other._bottLeft);
    _center = new Point(*other._center);
    _width = other._width;
    _height = other._height;
    _angle = other._angle;
}

void RectangularEnt::initializeEntity(double bottLeftX, double bottLeftY)
{
    _bottLeft = new Point(bottLeftX, bottLeftY);
    double ang_cos = cos(_angle);
    double ang_sin = sin(_angle);
    _center = new Point(_bottLeft->getX() + _width / 2.0 * ang_cos + _height / 2.0 * ang_sin,
        _bottLeft->getY() - _width / 2.0 * ang_sin + _height / 2.0 * ang_cos);
}

double RectangularEnt::collisionLength(SimEnt& other, Point& proj)
{
    switch (other.getShapeID())
    {
        case SimEnt::CIRCLE:
        case SimEnt::KHEPERA_ROBOT:
        {
            CircularEnt &converted = *dynamic_cast<CircularEnt*>(&other);
            Point clone(*_bottLeft);
            return check_and_divide(converted, clone, _width, _height, 1);
        }

        case SimEnt::RECTANGLE:
        default:
            return NO_COLLISION;
    }
}

void RectangularEnt::translate(double x, double y)
{
	_bottLeft->translate(x, y);
}

double RectangularEnt::check_and_divide(CircularEnt& other, Point& bottLeft, double width, double height, int level)
{
	if (level > DIVIDING_LEVEL)
		return INF_COLLISION;

	double ang_cos = cos(_angle);
	double ang_sin = sin(_angle);
	width /= 2.0;
	height /= 2.0;

	// center point is calculated as a result of multiplication of 3 transformation matrices (-translate bottLeft, rotate _angle, translate bottLeft)
	Point center(bottLeft.getX() + width * ang_cos - height * ang_sin, bottLeft.getY() + width * ang_sin + height * ang_cos);
	double radius = center.getDistance(bottLeft);

	double radiuses_sum = radius + other.getRadius();
	double centres_diff = center.getDistance(other.getCenter());

    if (centres_diff > radiuses_sum)
        return radiuses_sum - centres_diff;
	else
	{
		double max_coll = NO_COLLISION;
		level++;

		Point copy = Point(bottLeft);

		max_coll = max(max_coll, check_and_divide(other, bottLeft, width, height, level)); // bottom left
		bottLeft.translate(- height * ang_sin, height * ang_cos);
		max_coll = max(max_coll, check_and_divide(other, bottLeft, width, height, level)); // upper left
		bottLeft.setCoords(copy);
		bottLeft.translate(width * ang_cos, width * ang_sin);
		max_coll = max(max_coll, check_and_divide(other, bottLeft, width, height, level)); // bottom right
		max_coll = max(max_coll, check_and_divide(other, center, width, height, level)); // upper right
		bottLeft.setCoords(copy);

		return min(max_coll, radiuses_sum - centres_diff);
	}
}




/*
		Serialization format (integers in network-byte-order, doubles and floats in host-byte-order)

		+-------------------+--------------------------------------+-------------------+
		|                   |                                      |                   |
		|   SHAPE_ID        |              ENTITY_ID               |    MOVABLE        |
		|    8 bytes        |               16 bytes               |    8 bytes        |
		+-------------------+--------------------------------------+-------------------+
		|                                                                              |
		|                                 WEIGHT                                       |
		|                                32 bytes                                      |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   X1                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   Y1                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   X2                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   Y2                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   X3                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   Y3                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   X4                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+
		|                                                                              |
		|                                                                              |
		|                                   Y4                                         |
		|                                64 bytes                                      |
		|                                                                              |
		|                                                                              |
		+------------------------------------------------------------------------------+

							DATA_LENGTH = 576 bytes

*/

void RectangularEnt::serialize(Buffer& buffer)
{
    SimEnt::serialize(buffer);

    double ang_cos = cos(_angle);
    double ang_sin = sin(_angle);
    buffer.pack(_bottLeft->getX());
    buffer.pack(_bottLeft->getY());
    buffer.pack(_bottLeft->getX() - _height * ang_sin);
    buffer.pack(_bottLeft->getY() + _height * ang_cos);
    buffer.pack(_bottLeft->getX() - _height * ang_sin + _width * ang_cos);
    buffer.pack(_bottLeft->getY() + _height * ang_cos + _width * ang_sin);
    buffer.pack(_bottLeft->getX() + _width * ang_cos);
    buffer.pack(_bottLeft->getY() + _width * ang_sin);
}

void RectangularEnt::serialize(std::ofstream& file)
{
    double x = _bottLeft->getX();
    double y = _bottLeft->getY();

    SimEnt::serialize(file);
    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
    file.write(reinterpret_cast<const char*>(&y), sizeof(y));
    file.write(reinterpret_cast<const char*>(&_width), sizeof(_width));
    file.write(reinterpret_cast<const char*>(&_height), sizeof(_height));
    file.write(reinterpret_cast<const char*>(&_angle), sizeof(_angle));
}

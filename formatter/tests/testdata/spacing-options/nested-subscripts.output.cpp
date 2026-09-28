unsigned Blend(unsigned code)
{
    return matrix[6][code & 0xff] ^ matrix[3][code] ^
        cube[1][2][code & 0xff];
}
